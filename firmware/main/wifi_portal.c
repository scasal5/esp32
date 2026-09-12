#include "wifi_portal.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "wifi_portal";

#define DNS_PORT 53
#define PORTAL_URI "http://192.168.4.1"

static httpd_handle_t s_httpd;
static TaskHandle_t s_dns_task;
static volatile bool s_dns_run;
static int s_dns_sock = -1;
static char s_target[33];
static wifi_portal_on_pass_t s_on_pass;

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static void url_decode(char *s)
{
    char *r = s;
    char *w = s;
    while (*r) {
        if (*r == '+') {
            *w++ = ' ';
            r++;
        } else if (*r == '%' && hex_val(r[1]) >= 0 && hex_val(r[2]) >= 0) {
            *w++ = (char)((hex_val(r[1]) << 4) | hex_val(r[2]));
            r += 3;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

static bool form_get(char *body, const char *key, char *out, size_t n)
{
    const size_t klen = strlen(key);
    char *p = body;
    while (p != NULL && *p != '\0') {
        char *amp = strchr(p, '&');
        if (amp != NULL) {
            *amp = '\0';
        }
        char *eq = strchr(p, '=');
        if (eq != NULL) {
            *eq = '\0';
            url_decode(p);
            url_decode(eq + 1);
            if (strcmp(p, key) == 0) {
                strncpy(out, eq + 1, n - 1);
                out[n - 1] = '\0';
                return true;
            }
        }
        p = (amp != NULL) ? amp + 1 : NULL;
    }
    (void)klen;
    return false;
}

static void html_escape(const char *in, char *out, size_t n)
{
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j + 7 < n; i++) {
        if (in[i] == '&') {
            memcpy(out + j, "&amp;", 5);
            j += 5;
        } else if (in[i] == '<') {
            memcpy(out + j, "&lt;", 4);
            j += 4;
        } else if (in[i] == '>') {
            memcpy(out + j, "&gt;", 4);
            j += 4;
        } else if (in[i] == '"') {
            memcpy(out + j, "&quot;", 6);
            j += 6;
        } else {
            out[j++] = in[i];
        }
    }
    out[j] = '\0';
}

static esp_err_t send_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, "Redirect", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t root_get(httpd_req_t *req)
{
    char esc[96];
    html_escape(s_target, esc, sizeof(esc));

    char page[768];
    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head>"
             "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
             "<meta charset=\"utf-8\"><title>ws183-os</title>"
             "<style>"
             "body{font-family:sans-serif;background:#101418;color:#F2F4F8;"
             "margin:24px}"
             "input,button{width:100%%;box-sizing:border-box;padding:14px;"
             "margin:10px 0;font-size:18px;border-radius:10px;border:0}"
             "button{background:#3B82F6;color:#fff}"
             "</style></head><body>"
             "<h2>ws183-os</h2><p>Red: %s</p>"
             "<form method=\"POST\" action=\"/connect\">"
             "<input type=\"password\" name=\"pass\" placeholder=\"contrasena\" "
             "autofocus>"
             "<button type=\"submit\">Conectar</button></form>"
             "</body></html>",
             esc);

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t connect_post(httpd_req_t *req)
{
    char body[192];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "sin datos", HTTPD_RESP_USE_STRLEN);
    }
    body[len] = '\0';

    char pass[65] = { 0 };
    form_get(body, "pass", pass, sizeof(pass));

    ESP_LOGI(TAG, "portal submit ssid=%s pass_len=%u", s_target,
             (unsigned)strlen(pass));

    if (s_on_pass != NULL && s_target[0] != '\0') {
        s_on_pass(s_target, pass);
    }

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req,
                           "<!DOCTYPE html><html><body>"
                           "<p>Conectando. Podes volver a la placa.</p>"
                           "</body></html>",
                           HTTPD_RESP_USE_STRLEN);
}

static esp_err_t captive_get(httpd_req_t *req)
{
    return send_redirect(req);
}

static esp_err_t http_404(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    return send_redirect(req);
}

static void dns_task(void *arg)
{
    (void)arg;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGW(TAG, "dns socket: %d", errno);
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    s_dns_sock = sock;

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGW(TAG, "dns bind: %d", errno);
        close(sock);
        s_dns_sock = -1;
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (s_dns_run) {
        uint8_t req[256];
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        int n = recvfrom(sock, req, sizeof(req), 0, (struct sockaddr *)&src, &slen);
        if (n < (int)sizeof(uint16_t) * 6) {
            continue;
        }

        /* Copia la consulta y responde A 192.168.4.1 con puntero al nombre. */
        uint8_t reply[512];
        if (n > (int)sizeof(reply) - 16) {
            continue;
        }
        memcpy(reply, req, (size_t)n);
        reply[2] |= 0x80; /* QR */
        reply[3] = 0x80;  /* RA */
        reply[6] = 0;
        reply[7] = 1; /* an_count = 1 */

        int i = n;
        reply[i++] = 0xC0;
        reply[i++] = 0x0C;
        reply[i++] = 0x00;
        reply[i++] = 0x01; /* A */
        reply[i++] = 0x00;
        reply[i++] = 0x01; /* IN */
        reply[i++] = 0x00;
        reply[i++] = 0x00;
        reply[i++] = 0x00;
        reply[i++] = 30; /* TTL */
        reply[i++] = 0x00;
        reply[i++] = 0x04;
        reply[i++] = 192;
        reply[i++] = 168;
        reply[i++] = 4;
        reply[i++] = 1;

        sendto(sock, reply, (size_t)i, 0, (struct sockaddr *)&src, slen);
    }

    close(sock);
    s_dns_sock = -1;
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

static void set_dhcp_captive(void)
{
    esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap == NULL) {
        return;
    }
    static char uri[] = PORTAL_URI;
    esp_netif_dhcps_stop(ap);
    esp_err_t err = esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET,
                                           ESP_NETIF_CAPTIVEPORTAL_URI,
                                           uri, strlen(uri));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "dhcp captive: %s", esp_err_to_name(err));
    }
    esp_netif_dhcps_start(ap);
}

esp_err_t wifi_portal_start(const char *target_ssid, wifi_portal_on_pass_t cb)
{
    if (target_ssid == NULL || target_ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_portal_stop();

    strncpy(s_target, target_ssid, sizeof(s_target) - 1);
    s_target[sizeof(s_target) - 1] = '\0';
    s_on_pass = cb;

    set_dhcp_captive();

    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_open_sockets = 7;
    esp_err_t err = httpd_start(&s_httpd, &cfg);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = root_get
    };
    const httpd_uri_t connect = {
        .uri = "/connect", .method = HTTP_POST, .handler = connect_post
    };
    const httpd_uri_t gen204 = {
        .uri = "/generate_204", .method = HTTP_GET, .handler = captive_get
    };
    const httpd_uri_t hotspot = {
        .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_get
    };
    httpd_register_uri_handler(s_httpd, &root);
    httpd_register_uri_handler(s_httpd, &connect);
    httpd_register_uri_handler(s_httpd, &gen204);
    httpd_register_uri_handler(s_httpd, &hotspot);
    httpd_register_err_handler(s_httpd, HTTPD_404_NOT_FOUND, http_404);

    s_dns_run = true;
    if (xTaskCreate(dns_task, "dns", 3072, NULL, 5, &s_dns_task) != pdPASS) {
        s_dns_run = false;
        httpd_stop(s_httpd);
        s_httpd = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "portal up target=%s", s_target);
    return ESP_OK;
}

void wifi_portal_stop(void)
{
    s_dns_run = false;
    if (s_dns_sock >= 0) {
        shutdown(s_dns_sock, 0);
    }
    if (s_httpd != NULL) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    s_on_pass = NULL;
}

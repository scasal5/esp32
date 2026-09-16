#include "fondo_http.h"
#include "splash_gif.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"

#include "bsp/esp-bsp.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"

ESP_EVENT_DEFINE_BASE(FONDO_EVENT);

static const char *TAG = "fondo_http";

extern const uint8_t fondo_form_html_start[] asm("_binary_fondo_form_html_start");
extern const uint8_t fondo_form_html_end[] asm("_binary_fondo_form_html_end");

#define SPLASH_MAX   (1024 * 1024)

static httpd_handle_t s_httpd;
static SemaphoreHandle_t s_lock;

static bool s_pending;
static bool s_allowed;
static bool s_denied;
static bool s_have_mac;
static uint8_t s_mac[6];
static uint32_t s_ip;
static char s_ip_str[16];

static bool take_lock(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
    return s_lock != NULL && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE;
}

static bool png_ok(const uint8_t *p, size_t n)
{
    static const uint8_t mag[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a };
    if (n < 24 || memcmp(p, mag, 8) != 0) {
        return false;
    }
    unsigned w = ((unsigned)p[16] << 24) | ((unsigned)p[17] << 16) |
                 ((unsigned)p[18] << 8) | p[19];
    unsigned h = ((unsigned)p[20] << 24) | ((unsigned)p[21] << 16) |
                 ((unsigned)p[22] << 8) | p[23];
    return w > 0 && h > 0 && w <= (unsigned)BSP_LCD_H_RES &&
           h <= (unsigned)BSP_LCD_V_RES;
}

static bool peer_ip(httpd_req_t *req, uint32_t *out)
{
    int fd = httpd_req_to_sockfd(req);
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    memset(&ss, 0, sizeof(ss));
    if (fd < 0) {
        ESP_LOGW(TAG, "sockfd %d", fd);
        return false;
    }
    if (getpeername(fd, (struct sockaddr *)&ss, &slen) != 0) {
        ESP_LOGW(TAG, "getpeername errno=%d", errno);
        return false;
    }
    if (ss.ss_family == AF_INET) {
        *out = ((struct sockaddr_in *)&ss)->sin_addr.s_addr;
        return true;
    }
    /* httpd de ESP-IDF usa sockets dual stack: IPv4 llega como ::ffff:x.x.x.x */
    if (ss.ss_family == AF_INET6) {
        const uint8_t *b =
            (const uint8_t *)&((const struct sockaddr_in6 *)&ss)->sin6_addr;
        static const uint8_t mapped[12] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff
        };
        if (memcmp(b, mapped, sizeof(mapped)) == 0) {
            memcpy(out, b + 12, 4);
            return true;
        }
        ESP_LOGW(TAG, "peer IPv6 nativo");
        return false;
    }
    ESP_LOGW(TAG, "peer family=%d", (int)ss.ss_family);
    return false;
}

static bool mac_for_ip(uint32_t ip_nbo, uint8_t mac[6])
{
    ip4_addr_t ip;
    ip.addr = ip_nbo;
    esp_netif_t *en = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (en == NULL) {
        return false;
    }
    struct netif *n = (struct netif *)esp_netif_get_netif_impl(en);
    if (n == NULL) {
        return false;
    }

    struct eth_addr *eth = NULL;
    const ip4_addr_t *ipr = NULL;
    bool ok = false;
    LOCK_TCPIP_CORE();
    if (etharp_find_addr(n, &ip, &eth, &ipr) >= 0 && eth != NULL) {
        memcpy(mac, eth->addr, 6);
        ok = true;
    }
    UNLOCK_TCPIP_CORE();
    return ok;
}

static bool same_client(uint32_t ip, const uint8_t *mac, bool have_mac)
{
    if (s_have_mac && have_mac) {
        return memcmp(s_mac, mac, 6) == 0;
    }
    return s_ip == ip;
}

static void fill_client(fondo_client_t *c)
{
    memset(c, 0, sizeof(*c));
    memcpy(c->mac, s_mac, 6);
    c->have_mac = s_have_mac;
    strncpy(c->ip, s_ip_str, sizeof(c->ip) - 1);
}

static void remember(uint32_t ip, const uint8_t *mac, bool have_mac)
{
    s_ip = ip;
    s_have_mac = have_mac;
    if (have_mac) {
        memcpy(s_mac, mac, 6);
    } else {
        memset(s_mac, 0, 6);
    }
    snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR((esp_ip4_addr_t *)&ip));
}

static void send_html(httpd_req_t *req, const char *html)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, html);
}

static const char PAGE_WAIT[] =
    "<!DOCTYPE html><html><head>"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<meta charset=\"utf-8\"><title>ws183-os</title>"
    "<style>"
    "body{font-family:sans-serif;background:#0A0A0A;color:#FAFAFA;margin:24px}"
    ".spin{width:48px;height:48px;border:4px solid #2A2A2A;border-top-color:#FAFAFA;"
    "border-radius:50%;animation:s .8s linear infinite;margin:24px auto}"
    "@keyframes s{to{transform:rotate(360deg)}}"
    "</style></head><body>"
    "<div id=\"on\">"
    "<h2>ws183-os</h2>"
    "<div class=\"spin\"></div>"
    "<p>Esperando que acepten este celular en la placa.</p>"
    "</div>"
    "<div id=\"bye\" style=\"display:none\">"
    "<h2>ws183-os</h2>"
    "<p>Se cancelo en la placa.</p>"
    "<p>Volve a Fondo y genera el QR de nuevo.</p>"
    "</div>"
    "<script>"
    "function bye(){document.getElementById('on').style.display='none';"
    "document.getElementById('bye').style.display='block';}"
    "(function poll(){fetch('/status').then(r=>r.json()).then(j=>{"
    "if(j.state==='allowed'){location.reload();return;}"
    "if(j.state==='denied'){bye();return;}"
    "setTimeout(poll,400);}).catch(()=>bye());})();"
    "</script></body></html>";

static const char PAGE_NO[] =
    "<!DOCTYPE html><html><head>"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<meta charset=\"utf-8\"><title>ws183-os</title>"
    "<style>body{font-family:sans-serif;background:#0A0A0A;color:#FAFAFA;"
    "margin:24px}</style></head><body>"
    "<h2>ws183-os</h2>"
    "<p>La placa no acepto este celular.</p>"
    "</body></html>";

static const char PAGE_BUSY[] =
    "<!DOCTYPE html><html><head>"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<meta charset=\"utf-8\"><title>ws183-os</title>"
    "<style>body{font-family:sans-serif;background:#0A0A0A;color:#FAFAFA;"
    "margin:24px}</style></head><body>"
    "<h2>ws183-os</h2>"
    "<p>Hay otro celular en la placa.</p>"
    "</body></html>";

static esp_err_t send_form(httpd_req_t *req)
{
    uint32_t ip = 0;
    uint8_t mac[6] = { 0 };
    if (!peer_ip(req, &ip)) {
        ESP_LOGW(TAG, "sin IP de peer, se pide igual");
    }
    const bool have_mac = (ip != 0) && mac_for_ip(ip, mac);

    if (!take_lock()) {
        return ESP_FAIL;
    }

    bool form = false;
    bool no = false;
    bool busy = false;
    bool announce = false;
    fondo_client_t client;

    if (s_allowed && same_client(ip, mac, have_mac)) {
        form = true;
    } else if (s_allowed || s_pending) {
        if (same_client(ip, mac, have_mac)) {
            form = s_allowed;
        } else {
            busy = true;
        }
    } else if (s_denied && same_client(ip, mac, have_mac)) {
        no = true;
    } else {
        remember(ip, mac, have_mac);
        s_pending = true;
        s_allowed = false;
        s_denied = false;
        fill_client(&client);
        announce = true;
    }
    xSemaphoreGive(s_lock);

    if (announce) {
        if (have_mac) {
            ESP_LOGI(TAG, "client " MACSTR " %s", MAC2STR(mac), client.ip);
        } else {
            ESP_LOGI(TAG, "client %s (sin MAC)", client.ip);
        }
        esp_event_post(FONDO_EVENT, FONDO_EVENT_CLIENT, &client, sizeof(client), 0);
    }

    if (form) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_send(req, (const char *)fondo_form_html_start,
                               fondo_form_html_end - fondo_form_html_start);
    }
    if (no) {
        send_html(req, PAGE_NO);
        return ESP_OK;
    }
    if (busy) {
        httpd_resp_set_status(req, "403 Forbidden");
        send_html(req, PAGE_BUSY);
        return ESP_OK;
    }
    send_html(req, PAGE_WAIT);
    return ESP_OK;
}

static esp_err_t status_get(httpd_req_t *req)
{
    const char *state = "idle";
    if (take_lock()) {
        if (s_allowed) {
            state = "allowed";
        } else if (s_pending) {
            state = "wait";
        } else if (s_denied) {
            state = "denied";
        }
        xSemaphoreGive(s_lock);
    }
    char json[40];
    snprintf(json, sizeof(json), "{\"state\":\"%s\"}", state);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static bool client_allowed(httpd_req_t *req)
{
    uint32_t ip = 0;
    uint8_t mac[6] = { 0 };
    if (!peer_ip(req, &ip)) {
        return false;
    }
    const bool have_mac = mac_for_ip(ip, mac);
    if (!take_lock()) {
        return false;
    }
    const bool ok = s_allowed && same_client(ip, mac, have_mac);
    xSemaphoreGive(s_lock);
    return ok;
}

static esp_err_t upload_post(httpd_req_t *req)
{
    if (!client_allowed(req)) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"no permitido\"}");
    }

    const int len = req->content_len;
    if (len <= 0 || len > SPLASH_MAX) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"archivo grande\"}");
    }

    uint8_t *buf = heap_caps_malloc((size_t)len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = malloc((size_t)len);
    }
    if (buf == NULL) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"sin memoria\"}");
    }

    int got = 0;
    while (got < len) {
        if (!fondo_http_allowed()) {
            free(buf);
            httpd_resp_set_status(req, "403 Forbidden");
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"cancelado\"}");
        }
        const int n = httpd_req_recv(req, (char *)buf + got, (size_t)(len - got));
        if (n <= 0) {
            free(buf);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"corte\"}");
        }
        got += n;
    }

    httpd_resp_set_type(req, "application/json");
    const bool is_png = png_ok(buf, (size_t)got);
    const bool is_gif = (got >= 13 &&
                         (memcmp(buf, "GIF87a", 6) == 0 || memcmp(buf, "GIF89a", 6) == 0));
    if (!is_png && !is_gif) {
        free(buf);
        esp_event_post(FONDO_EVENT, FONDO_EVENT_FAIL, NULL, 0, 0);
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"manda png o gif 240x284\"}");
    }

    const esp_err_t err = splash_gif_install(buf, (size_t)got);
    free(buf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "install: %s", esp_err_to_name(err));
        esp_event_post(FONDO_EVENT, FONDO_EVENT_FAIL, NULL, 0, 0);
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"no se pudo aplicar\"}");
    }

    ESP_LOGI(TAG, "splash.png %d bytes", got);
    esp_event_post(FONDO_EVENT, FONDO_EVENT_SAVED, NULL, 0, 0);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t fondo_http_start(void)
{
#if !CONFIG_WS183_COMPANION
    ESP_LOGW(TAG, "companion off: Fondo HTTP deshabilitado");
    return ESP_ERR_NOT_SUPPORTED;
#endif
    fondo_http_stop();

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_open_sockets = 7;
    cfg.max_uri_handlers = 8;
    cfg.stack_size = 16384;
    cfg.recv_wait_timeout = 30;
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    esp_err_t err = httpd_start(&s_httpd, &cfg);
    if (err != ESP_OK) {
        s_httpd = NULL;
        return err;
    }

    static const httpd_uri_t uri_root = {
        .uri = "/", .method = HTTP_GET, .handler = send_form
    };
    static const httpd_uri_t uri_upload = {
        .uri = "/upload", .method = HTTP_POST, .handler = upload_post
    };
    static const httpd_uri_t uri_status = {
        .uri = "/status", .method = HTTP_GET, .handler = status_get
    };
    httpd_register_uri_handler(s_httpd, &uri_root);
    httpd_register_uri_handler(s_httpd, &uri_upload);
    httpd_register_uri_handler(s_httpd, &uri_status);
    ESP_LOGI(TAG, "http up");
    return ESP_OK;
}

void fondo_http_stop(void)
{
    if (take_lock()) {
        s_pending = false;
        s_allowed = false;
        s_denied = true;
        xSemaphoreGive(s_lock);
    }
    if (s_httpd != NULL) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    if (take_lock()) {
        s_denied = false;
        s_have_mac = false;
        s_ip = 0;
        s_ip_str[0] = '\0';
        memset(s_mac, 0, 6);
        xSemaphoreGive(s_lock);
    }
}

void fondo_http_allow(void)
{
    if (!take_lock()) {
        return;
    }
    if (s_pending) {
        s_pending = false;
        s_allowed = true;
        s_denied = false;
        if (s_have_mac) {
            ESP_LOGI(TAG, "allow " MACSTR, MAC2STR(s_mac));
        } else {
            ESP_LOGI(TAG, "allow %s", s_ip_str);
        }
    }
    xSemaphoreGive(s_lock);
}

void fondo_http_deny(void)
{
    if (!take_lock()) {
        return;
    }
    s_pending = false;
    s_allowed = false;
    s_denied = true;
    ESP_LOGI(TAG, "deny %s", s_ip_str[0] ? s_ip_str : "-");
    xSemaphoreGive(s_lock);
}

bool fondo_http_allowed(void)
{
    if (!take_lock()) {
        return false;
    }
    const bool ok = s_allowed;
    xSemaphoreGive(s_lock);
    return ok;
}

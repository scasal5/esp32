#include "fondo_http.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

#define SPLASH_PATH  BSP_SPIFFS_MOUNT_POINT "/splash.gif"
#define SPLASH_NEW   BSP_SPIFFS_MOUNT_POINT "/splash.new"
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

static bool gif_ok(const uint8_t *p, size_t n)
{
    if (n < 13 || (memcmp(p, "GIF87a", 6) != 0 && memcmp(p, "GIF89a", 6) != 0)) {
        return false;
    }
    unsigned w = (unsigned)(p[6] | (p[7] << 8));
    unsigned h = (unsigned)(p[8] | (p[9] << 8));
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
    "<meta charset=\"utf-8\"><meta http-equiv=\"refresh\" content=\"2\">"
    "<title>ws183-os</title>"
    "<style>body{font-family:sans-serif;background:#0A0A0A;color:#FAFAFA;"
    "margin:24px}</style></head><body>"
    "<h2>ws183-os</h2>"
    "<p>Esperando que acepten este celular en la placa.</p>"
    "</body></html>";

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

static const char PAGE_FORM[] =
    "<!DOCTYPE html><html><head>"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<meta charset=\"utf-8\"><title>ws183-os</title>"
    "<style>"
    "body{font-family:sans-serif;background:#0A0A0A;color:#FAFAFA;margin:24px}"
    "input,button{width:100%;box-sizing:border-box;padding:14px;margin:10px 0;"
    "font-size:18px;border-radius:10px;border:0}"
    "button{background:#FAFAFA;color:#0A0A0A}"
    ".hide{display:none}"
    "</style></head><body>"
    "<h2>Fondo</h2>"
    "<p>PNG, JPG o GIF. La placa lo guarda a 240x284.</p>"
    "<div id=\"box\">"
    "<input id=\"f\" type=\"file\" accept=\"image/png,image/jpeg,image/gif,.png,.jpg,.jpeg,.gif\">"
    "<button type=\"button\" id=\"go\">Subir</button>"
    "</div>"
    "<div id=\"wait\" class=\"hide\"><p>Subiendo...</p></div>"
    "<div id=\"ok\" class=\"hide\"><p>Listo. El fondo queda en la placa.</p></div>"
    "<div id=\"fail\" class=\"hide\"><p id=\"err\">No se pudo guardar.</p>"
    "<button type=\"button\" id=\"retry\">Reintentar</button></div>"
    "<script>"
    "const $=id=>document.getElementById(id);"
    "function show(id){['box','wait','ok','fail'].forEach(x=>$(x).classList.add('hide'));"
    "$(id).classList.remove('hide');}"
    "$('retry').onclick=()=>show('box');"
    "function pal(){const p=new Uint8Array(768);for(let i=0;i<256;i++){"
    "p[i*3]=(i>>5)*36;p[i*3+1]=((i>>2)&7)*36;p[i*3+2]=(i&3)*85;}return p;}"
    "function idx(r,g,b){return ((r>>5)<<5)|((g>>5)<<2)|(b>>6);}"
    "function gifFrom(imgd,w,h){"
    "const px=new Uint8Array(w*h),d=imgd.data;"
    "for(let i=0,p=0;i<px.length;i++,p+=4) px[i]=idx(d[p],d[p+1],d[p+2]);"
    "const out=[];const u8=v=>out.push(v&255);const u16=v=>{u8(v);u8(v>>8);};"
    "const s='GIF87a';for(let i=0;i<6;i++) u8(s.charCodeAt(i));"
    "u16(w);u16(h);u8(0xF7);u8(0);u8(0);"
    "const p=pal();for(let i=0;i<p.length;i++) u8(p[i]);"
    "u8(0x2C);u16(0);u16(0);u16(w);u16(h);u8(0);u8(8);"
    "const stream=[];let buf=0,nbits=0;"
    "const emit=(code)=>{buf|=code<<nbits;nbits+=9;while(nbits>=8){"
    "stream.push(buf&255);buf>>=8;nbits-=8;}};"
    "emit(256);let c=0;for(let i=0;i<px.length;i++){emit(px[i]);c++;"
    "if(c===100){emit(256);c=0;}}emit(257);if(nbits) stream.push(buf&255);"
    "for(let i=0;i<stream.length;){const n=Math.min(255,stream.length-i);u8(n);"
    "for(let j=0;j<n;j++) u8(stream[i++]);}u8(0);u8(0x3B);"
    "return new Uint8Array(out);}"
    "function cover(img){const w=240,h=284,cv=document.createElement('canvas');"
    "cv.width=w;cv.height=h;const s=Math.max(w/img.width,h/img.height);"
    "const dw=img.width*s,dh=img.height*s;"
    "cv.getContext('2d').drawImage(img,(w-dw)/2,(h-dh)/2,dw,dh);"
    "return gifFrom(cv.getContext('2d').getImageData(0,0,w,h),w,h);}"
    "function gifFits(u){if(u.length<13)return false;"
    "const t=String.fromCharCode(u[0],u[1],u[2],u[3],u[4],u[5]);"
    "if(t!=='GIF87a'&&t!=='GIF89a')return false;"
    "const w=u[6]|u[7]<<8,h=u[8]|u[9]<<8;"
    "return w>0&&h>0&&w<=240&&h<=284&&u.length<=1048576;}"
    "async function body(file){"
    "if(file.type==='image/gif'||/\\.gif$/i.test(file.name)){"
    "const u=new Uint8Array(await file.arrayBuffer());if(gifFits(u))return u;}"
    "const url=URL.createObjectURL(file);"
    "try{const img=await new Promise((ok,bad)=>{const i=new Image();"
    "i.onload=()=>ok(i);i.onerror=bad;i.src=url;});return cover(img);}"
    "finally{URL.revokeObjectURL(url);}}"
    "$('go').onclick=async()=>{"
    "const file=$('f').files[0];if(!file)return;show('wait');"
    "try{const bin=await body(file);"
    "const r=await fetch('/upload',{method:'POST',body:bin,"
    "headers:{'Content-Type':'image/gif'}});"
    "const j=await r.json();if(j.ok)show('ok');else{$('err').textContent=j.err||'fallo';show('fail');}}"
    "catch(e){$('err').textContent='no se pudo enviar';show('fail');}};"
    "</script></body></html>";

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
        send_html(req, PAGE_FORM);
        return ESP_OK;
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

static esp_err_t save_gif(const uint8_t *buf, size_t n)
{
    FILE *f = fopen(SPLASH_NEW, "wb");
    if (f == NULL) {
        return ESP_FAIL;
    }
    const size_t w = fwrite(buf, 1, n, f);
    fclose(f);
    if (w != n) {
        unlink(SPLASH_NEW);
        return ESP_FAIL;
    }
    unlink(SPLASH_PATH);
    if (rename(SPLASH_NEW, SPLASH_PATH) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
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
    if (!gif_ok(buf, (size_t)got)) {
        free(buf);
        esp_event_post(FONDO_EVENT, FONDO_EVENT_FAIL, NULL, 0, 0);
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"no es un GIF de 240x284\"}");
    }

    const esp_err_t err = save_gif(buf, (size_t)got);
    free(buf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save: %s", esp_err_to_name(err));
        esp_event_post(FONDO_EVENT, FONDO_EVENT_FAIL, NULL, 0, 0);
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"no se pudo guardar\"}");
    }

    ESP_LOGI(TAG, "splash.gif %d bytes", got);
    esp_event_post(FONDO_EVENT, FONDO_EVENT_SAVED, NULL, 0, 0);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t fondo_http_start(void)
{
    fondo_http_stop();

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_open_sockets = 7;
    cfg.max_uri_handlers = 8;
    cfg.stack_size = 8192;
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
    httpd_register_uri_handler(s_httpd, &uri_root);
    httpd_register_uri_handler(s_httpd, &uri_upload);
    ESP_LOGI(TAG, "http up");
    return ESP_OK;
}

void fondo_http_stop(void)
{
    if (s_httpd != NULL) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    if (take_lock()) {
        s_pending = false;
        s_allowed = false;
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

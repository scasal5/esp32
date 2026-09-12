#include "svc_wifi.h"
#include "wifi_portal.h"

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

ESP_EVENT_DEFINE_BASE(SVC_WIFI_EVENT);

static const char *TAG = "wifi";

#define WIFI_RECORD_BUFFER 64
#define NVS_NS             "wifi"
#define NVS_KEY_SSID       "ssid"
#define NVS_KEY_PASS       "pass"

static SemaphoreHandle_t s_lock;
static svc_wifi_ap_t s_results[SVC_WIFI_MAX_RESULTS];
static size_t s_result_count;
static bool s_inited;
static bool s_sta_up;
static bool s_connected;
static bool s_connecting;
static bool s_scan_in_progress;
static bool s_scan_pending;
static bool s_prov_on;
static char s_sta_ssid[33];
static char s_saved_ssid[33];
static char s_saved_pass[65];
static char s_ap_ssid[16];
static char s_qr[40];
static char s_prov_target[33];
static esp_netif_t *s_ap_netif;

static bool take_lock(void)
{
    return s_lock != NULL && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE;
}

static void sort_results(void)
{
    for (size_t i = 1; i < s_result_count; i++) {
        svc_wifi_ap_t current = s_results[i];
        size_t j = i;
        while (j > 0 && s_results[j - 1].rssi < current.rssi) {
            s_results[j] = s_results[j - 1];
            j--;
        }
        s_results[j] = current;
    }
}

static void fill_ap(svc_wifi_ap_t *dst, const wifi_ap_record_t *src)
{
    strncpy(dst->ssid, (const char *)src->ssid, sizeof(dst->ssid) - 1);
    dst->ssid[sizeof(dst->ssid) - 1] = '\0';
    dst->rssi = src->rssi;
    dst->open = (src->authmode == WIFI_AUTH_OPEN);
}

static esp_err_t scan_start_now(void)
{
    wifi_scan_config_t config = { 0 };
    return esp_wifi_scan_start(&config, false);
}

static void creds_save(const char *ssid, const char *pass)
{
    if (ssid == NULL || ssid[0] == '\0' || pass == NULL || pass[0] == '\0') {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_str(h, NVS_KEY_SSID, ssid);
    nvs_set_str(h, NVS_KEY_PASS, pass);
    nvs_commit(h);
    nvs_close(h);
}

static void creds_clear(void)
{
    s_saved_ssid[0] = '\0';
    s_saved_pass[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_erase_key(h, NVS_KEY_SSID);
    nvs_erase_key(h, NVS_KEY_PASS);
    nvs_commit(h);
    nvs_close(h);
}

static void creds_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t ssid_len = sizeof(s_saved_ssid);
    size_t pass_len = sizeof(s_saved_pass);
    if (nvs_get_str(h, NVS_KEY_SSID, s_saved_ssid, &ssid_len) != ESP_OK) {
        s_saved_ssid[0] = '\0';
    }
    if (nvs_get_str(h, NVS_KEY_PASS, s_saved_pass, &pass_len) != ESP_OK) {
        s_saved_pass[0] = '\0';
    }
    nvs_close(h);
    /* No reconectar a una red abierta guardada por error. */
    if (s_saved_ssid[0] != '\0' && s_saved_pass[0] == '\0') {
        ESP_LOGW(TAG, "nvs: red abierta, se ignora");
        creds_clear();
    }
}

static void make_ap_name(void)
{
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "ws183-%02X%02X", mac[4], mac[5]);
    snprintf(s_qr, sizeof(s_qr), "WIFI:T:nopass;S:%s;;", s_ap_ssid);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    if (event_id == WIFI_EVENT_STA_START) {
        bool do_scan = false;
        bool do_auto = false;
        char auto_ssid[33] = { 0 };
        char auto_pass[65] = { 0 };
        if (take_lock()) {
            s_sta_up = true;
            if (s_scan_pending) {
                s_scan_pending = false;
                s_scan_in_progress = true;
                do_scan = true;
            }
            if (s_saved_ssid[0] != '\0') {
                strncpy(auto_ssid, s_saved_ssid, sizeof(auto_ssid) - 1);
                strncpy(auto_pass, s_saved_pass, sizeof(auto_pass) - 1);
                do_auto = true;
            }
            xSemaphoreGive(s_lock);
        }
        ESP_LOGI(TAG, "STA up");
        if (do_scan) {
            esp_err_t err = scan_start_now();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "scan: %s", esp_err_to_name(err));
                if (take_lock()) {
                    s_scan_in_progress = false;
                    xSemaphoreGive(s_lock);
                }
            }
        }
        if (do_auto) {
            ESP_LOGI(TAG, "auto connect %s", auto_ssid);
            svc_wifi_connect(auto_ssid, auto_pass);
        }
        return;
    }

    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = event_data;
        const uint8_t reason = d != NULL ? d->reason : 0;
        bool fail = false;
        if (take_lock()) {
            s_connected = false;
            s_sta_ssid[0] = '\0';
            if (s_connecting && reason != WIFI_REASON_ASSOC_LEAVE) {
                s_connecting = false;
                fail = true;
            }
            xSemaphoreGive(s_lock);
        }
        if (fail) {
            ESP_LOGW(TAG, "connect fail reason=%u", (unsigned)reason);
            esp_event_post(SVC_WIFI_EVENT, SVC_WIFI_EVENT_CONNECT_FAIL, NULL, 0, 0);
        }
        return;
    }

    if (event_id == WIFI_EVENT_STA_STOP) {
        if (take_lock()) {
            s_sta_up = false;
            s_connected = false;
            s_sta_ssid[0] = '\0';
            xSemaphoreGive(s_lock);
        }
        return;
    }

    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "portal client");
        esp_event_post(SVC_WIFI_EVENT, SVC_WIFI_EVENT_PROV_CLIENT, NULL, 0, 0);
        return;
    }

    if (event_id != WIFI_EVENT_SCAN_DONE) {
        return;
    }

    uint16_t ap_count = 0;
    esp_err_t err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan results: %s", esp_err_to_name(err));
    }

    /* Estatico: el loop de eventos tiene 2304 bytes de stack. */
    static wifi_ap_record_t records[WIFI_RECORD_BUFFER];
    uint16_t record_count = ap_count;
    if (record_count > WIFI_RECORD_BUFFER) {
        record_count = WIFI_RECORD_BUFFER;
    }
    if (record_count > 0) {
        err = esp_wifi_scan_get_ap_records(&record_count, records);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "scan records: %s", esp_err_to_name(err));
            record_count = 0;
        }
    }

    if (!take_lock()) {
        return;
    }

    s_result_count = 0;
    for (uint16_t i = 0; i < record_count; i++) {
        if (records[i].ssid[0] == '\0') {
            continue;
        }

        size_t existing = s_result_count;
        for (size_t j = 0; j < s_result_count; j++) {
            if (strncmp((const char *)records[i].ssid, s_results[j].ssid, 32) == 0) {
                existing = j;
                break;
            }
        }

        if (existing < s_result_count) {
            if (records[i].rssi > s_results[existing].rssi) {
                fill_ap(&s_results[existing], &records[i]);
            }
            continue;
        }

        size_t target = s_result_count;
        if (target == SVC_WIFI_MAX_RESULTS) {
            target = 0;
            for (size_t j = 1; j < s_result_count; j++) {
                if (s_results[j].rssi < s_results[target].rssi) {
                    target = j;
                }
            }
            if (records[i].rssi <= s_results[target].rssi) {
                continue;
            }
        } else {
            s_result_count++;
        }

        fill_ap(&s_results[target], &records[i]);
    }
    sort_results();
    s_scan_in_progress = false;
    size_t result_count = s_result_count;
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "scan %u redes", (unsigned)result_count);
    esp_event_post(SVC_WIFI_EVENT, SVC_WIFI_EVENT_SCAN_DONE, NULL, 0, 0);
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    if (event_id == IP_EVENT_STA_GOT_IP) {
        if (s_saved_pass[0] == '\0') {
            ESP_LOGW(TAG, "IP de red abierta, se corta");
            creds_clear();
            (void)esp_wifi_disconnect();
            return;
        }
        const ip_event_got_ip_t *got = event_data;
        if (take_lock()) {
            s_connected = true;
            s_connecting = false;
            xSemaphoreGive(s_lock);
        }
        if (got != NULL) {
            ESP_LOGI(TAG, "IP up " IPSTR, IP2STR(&got->ip_info.ip));
        } else {
            ESP_LOGI(TAG, "IP up");
        }
        creds_save(s_sta_ssid, s_saved_pass);
        svc_wifi_prov_stop();
        esp_event_post(SVC_WIFI_EVENT, SVC_WIFI_EVENT_CONNECTED, NULL, 0, 0);
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
        if (take_lock()) {
            s_connected = false;
            xSemaphoreGive(s_lock);
        }
    }
}

static void start_cleanup(void)
{
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, ESP_EVENT_ANY_ID, ip_event_handler);
    if (s_lock != NULL) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
    }
    esp_wifi_deinit();
}

static void on_portal_pass(const char *ssid, const char *pass)
{
    svc_wifi_connect(ssid, pass);
}

esp_err_t svc_wifi_start(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    /* NVS es del driver (calibracion PHY) y del namespace "wifi". */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs: %s, se formatea", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (esp_netif_create_default_wifi_sta() == NULL) {
        ESP_LOGW(TAG, "no se pudo crear la interfaz STA");
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&config);
    if (err != ESP_OK) {
        return err;
    }

    /* RAM: el driver no escribe SSID ni claves. Las nuestras van al ns wifi. */
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        esp_wifi_deinit();
        return err;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        esp_wifi_deinit();
        return ESP_ERR_NO_MEM;
    }

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     wifi_event_handler, NULL);
    if (err == ESP_OK) {
        err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                         ip_event_handler, NULL);
    }
    if (err != ESP_OK) {
        start_cleanup();
        return err;
    }

    make_ap_name();
    creds_load();

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "start: %s", esp_err_to_name(err));
        start_cleanup();
        return err;
    }

    s_inited = true;
    ESP_LOGI(TAG, "STA starting");
    return ESP_OK;
}

esp_err_t svc_wifi_scan(void)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!take_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_scan_in_progress) {
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }
    if (s_connecting) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_WIFI_STATE;
    }

    if (!s_sta_up) {
        s_scan_pending = true;
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "scan diferido, STA no listo");
        return ESP_OK;
    }

    s_scan_in_progress = true;
    xSemaphoreGive(s_lock);

    esp_err_t err = scan_start_now();
    if (err != ESP_OK && take_lock()) {
        s_scan_in_progress = false;
        xSemaphoreGive(s_lock);
    }
    return err;
}

size_t svc_wifi_copy_results(svc_wifi_ap_t *out, size_t max)
{
    if (out == NULL || max == 0 || s_lock == NULL) {
        return 0;
    }
    if (!take_lock()) {
        return 0;
    }

    size_t count = s_result_count < max ? s_result_count : max;
    memcpy(out, s_results, count * sizeof(*out));
    xSemaphoreGive(s_lock);
    return count;
}

bool svc_wifi_connected(void)
{
    return s_connected;
}

const char *svc_wifi_sta_ssid(void)
{
    return s_sta_ssid;
}

void svc_wifi_disconnect(void)
{
    if (take_lock()) {
        s_connecting = false;
        s_connected = false;
        s_sta_ssid[0] = '\0';
        xSemaphoreGive(s_lock);
    }
    creds_clear();
    if (s_inited) {
        (void)esp_wifi_disconnect();
    }
    ESP_LOGI(TAG, "STA down");
}

esp_err_t svc_wifi_connect(const char *ssid, const char *pass)
{
    if (!s_inited || ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    if (pass == NULL || pass[0] == '\0') {
        ESP_LOGW(TAG, "red publica, no se conecta");
        return ESP_ERR_NOT_SUPPORTED;
    }

    wifi_config_t cfg = { 0 };
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;

    if (take_lock()) {
        s_connecting = true;
        strncpy(s_sta_ssid, ssid, sizeof(s_sta_ssid) - 1);
        s_sta_ssid[sizeof(s_sta_ssid) - 1] = '\0';
        strncpy(s_saved_ssid, ssid, sizeof(s_saved_ssid) - 1);
        strncpy(s_saved_pass, pass != NULL ? pass : "", sizeof(s_saved_pass) - 1);
        xSemaphoreGive(s_lock);
    }

    ESP_LOGI(TAG, "connect %s pass_len=%u", ssid,
             (unsigned)(pass != NULL ? strlen(pass) : 0));

    (void)esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        if (take_lock()) {
            s_connecting = false;
            xSemaphoreGive(s_lock);
        }
        return err;
    }
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        if (take_lock()) {
            s_connecting = false;
            xSemaphoreGive(s_lock);
        }
        return err;
    }
    return ESP_OK;
}

esp_err_t svc_wifi_prov_start(const char *ssid)
{
    if (!s_inited || ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    strncpy(s_prov_target, ssid, sizeof(s_prov_target) - 1);
    s_prov_target[sizeof(s_prov_target) - 1] = '\0';

    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (s_ap_netif == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t ap = { 0 };
    strncpy((char *)ap.ap.ssid, s_ap_ssid, sizeof(ap.ap.ssid) - 1);
    ap.ap.ssid_len = strlen(s_ap_ssid);
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.channel = 1;
    err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err != ESP_OK) {
        (void)esp_wifi_set_mode(WIFI_MODE_STA);
        return err;
    }

    err = wifi_portal_start(s_prov_target, on_portal_pass);
    if (err != ESP_OK) {
        (void)esp_wifi_set_mode(WIFI_MODE_STA);
        return err;
    }

    s_prov_on = true;
    ESP_LOGI(TAG, "SoftAP %s QR=%s target=%s", s_ap_ssid, s_qr, s_prov_target);
    return ESP_OK;
}

void svc_wifi_prov_stop(void)
{
    if (!s_prov_on) {
        wifi_portal_stop();
        return;
    }
    s_prov_on = false;
    wifi_portal_stop();
    if (s_inited) {
        (void)esp_wifi_set_mode(WIFI_MODE_STA);
    }
    ESP_LOGI(TAG, "SoftAP off");
}

bool svc_wifi_prov_active(void)
{
    return s_prov_on;
}

const char *svc_wifi_prov_ap_ssid(void)
{
    return s_ap_ssid;
}

const char *svc_wifi_prov_qr(void)
{
    return s_qr;
}

const char *svc_wifi_prov_target(void)
{
    return s_prov_target;
}

static int cmd_wifi(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("sta_up=%d connected=%d ssid=%s\n", (int)s_sta_up, (int)s_connected,
           s_sta_ssid[0] ? s_sta_ssid : "-");
    printf("prov=%d ap=%s target=%s\n", (int)s_prov_on,
           s_ap_ssid[0] ? s_ap_ssid : "-",
           s_prov_target[0] ? s_prov_target : "-");

    svc_wifi_ap_t list[SVC_WIFI_MAX_RESULTS];
    size_t n = svc_wifi_copy_results(list, SVC_WIFI_MAX_RESULTS);
    printf("scan %u\n", (unsigned)n);
    for (size_t i = 0; i < n; i++) {
        printf("  %s  %d dBm%s\n", list[i].ssid, (int)list[i].rssi,
               list[i].open ? "" : " *");
    }
    return 0;
}

static int cmd_wifiscan(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = svc_wifi_scan();
    printf("wifiscan: %s\n", esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
}

static int cmd_wifiprov(int argc, char **argv)
{
    if (argc < 2) {
        printf("uso: wifiprov <ssid>\n");
        return 1;
    }
    esp_err_t err = svc_wifi_prov_start(argv[1]);
    printf("wifiprov: %s ap=%s qr=%s\n", esp_err_to_name(err), s_ap_ssid, s_qr);
    return err == ESP_OK ? 0 : 1;
}

static int cmd_wificonnect(int argc, char **argv)
{
    if (argc < 3) {
        printf("uso: wificonnect <ssid> <pass>\n");
        return 1;
    }
    esp_err_t err = svc_wifi_connect(argv[1], argv[2]);
    printf("wificonnect: %s\n", esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
}

static int cmd_wifidisconnect(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    svc_wifi_disconnect();
    printf("wifidisconnect ok\n");
    return 0;
}

static int cmd_wifistop(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    svc_wifi_prov_stop();
    printf("wifistop ok\n");
    return 0;
}

void svc_wifi_register_console(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "wifi", .help = "Estado STA/SoftAP y ultimo scan",
          .func = &cmd_wifi },
        { .command = "wifiscan", .help = "Dispara un scan",
          .func = &cmd_wifiscan },
        { .command = "wifiprov", .help = "Abre SoftAP+portal para un SSID",
          .hint = "<ssid>", .func = &cmd_wifiprov },
        { .command = "wificonnect", .help = "Conecta STA a una red con clave",
          .hint = "<ssid> <pass>", .func = &cmd_wificonnect },
        { .command = "wifidisconnect", .help = "Corta el STA y borra NVS wifi",
          .func = &cmd_wifidisconnect },
        { .command = "wifistop", .help = "Cierra el SoftAP",
          .func = &cmd_wifistop },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_console_cmd_register(&cmds[i]);
    }
}

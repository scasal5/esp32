#include "svc_wifi.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"

ESP_EVENT_DEFINE_BASE(SVC_WIFI_EVENT);

static const char *TAG = "wifi";

#define WIFI_RECORD_BUFFER 64

static SemaphoreHandle_t s_lock;
static svc_wifi_ap_t s_results[SVC_WIFI_MAX_RESULTS];
static size_t s_result_count;
static bool s_inited;
static bool s_sta_up;
static bool s_scan_in_progress;
static bool s_scan_pending;

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

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_data;

    if (event_id == WIFI_EVENT_STA_START) {
        bool do_scan = false;
        if (take_lock()) {
            s_sta_up = true;
            if (s_scan_pending) {
                s_scan_pending = false;
                s_scan_in_progress = true;
                do_scan = true;
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

static void start_cleanup(void)
{
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler);
    if (s_lock != NULL) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
    }
    esp_wifi_deinit();
}

esp_err_t svc_wifi_start(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    /* NVS es del driver (calibracion PHY). No se guardan credenciales. */
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

    /* RAM: el driver no escribe SSID ni claves en NVS. */
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
    if (err != ESP_OK) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        esp_wifi_deinit();
        return err;
    }

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

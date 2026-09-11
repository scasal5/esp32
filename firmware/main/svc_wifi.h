#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_WIFI_MAX_RESULTS 16

ESP_EVENT_DECLARE_BASE(SVC_WIFI_EVENT);

typedef enum {
    SVC_WIFI_EVENT_SCAN_DONE,
} svc_wifi_event_id_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool open;
} svc_wifi_ap_t;

esp_err_t svc_wifi_start(void);
esp_err_t svc_wifi_scan(void);
size_t svc_wifi_copy_results(svc_wifi_ap_t *out, size_t max);

#ifdef __cplusplus
}
#endif

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
    SVC_WIFI_EVENT_CONNECTED,
    SVC_WIFI_EVENT_CONNECT_FAIL,
} svc_wifi_event_id_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool open;
} svc_wifi_ap_t;

esp_err_t svc_wifi_start(void);
esp_err_t svc_wifi_scan(void);
size_t svc_wifi_copy_results(svc_wifi_ap_t *out, size_t max);
bool svc_wifi_connected(void);
const char *svc_wifi_sta_ssid(void);

/* pass vacio: red abierta. No loguea la clave. */
esp_err_t svc_wifi_connect(const char *ssid, const char *pass);

/*
 * SoftAP abierto ws183-XXXX + portal en 192.168.4.1 para la red `ssid`.
 * El QR es WIFI:T:nopass;S:ws183-XXXX;;
 */
esp_err_t svc_wifi_prov_start(const char *ssid);
void svc_wifi_prov_stop(void);
bool svc_wifi_prov_active(void);
const char *svc_wifi_prov_ap_ssid(void);
const char *svc_wifi_prov_qr(void);
const char *svc_wifi_prov_target(void);

void svc_wifi_register_console(void);

#ifdef __cplusplus
}
#endif

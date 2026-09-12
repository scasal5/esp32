#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(FONDO_EVENT);

typedef enum {
    FONDO_EVENT_CLIENT,
    FONDO_EVENT_SAVED,
    FONDO_EVENT_FAIL,
} fondo_event_id_t;

typedef struct {
    uint8_t mac[6];
    bool have_mac;
    char ip[16];
} fondo_client_t;

/* HTTP en la IP STA, solo mientras Fondo esta abierta. */
esp_err_t fondo_http_start(void);
void fondo_http_stop(void);
void fondo_http_allow(void);
void fondo_http_deny(void);
bool fondo_http_allowed(void);

#ifdef __cplusplus
}
#endif

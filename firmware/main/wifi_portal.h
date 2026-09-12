#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wifi_portal_on_pass_t)(const char *ssid, const char *pass);

esp_err_t wifi_portal_start(const char *target_ssid, wifi_portal_on_pass_t cb);
void wifi_portal_stop(void);

#ifdef __cplusplus
}
#endif

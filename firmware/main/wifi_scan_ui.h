#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_scan_ui_init(void);
void wifi_scan_ui_open(void);
void wifi_scan_ui_close(void);
bool wifi_scan_ui_is_open(void);

#ifdef __cplusplus
}
#endif
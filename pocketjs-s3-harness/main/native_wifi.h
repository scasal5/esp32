#pragma once
#include <stdbool.h>
#include "esp_err.h"
esp_err_t ws_wifi_start(void);
bool ws_wifi_connected(void);
esp_err_t ws_wifi_connect(const char *ssid,const char *password);
void ws_wifi_reconnect(void);

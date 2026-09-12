#pragma once

#include "esp_err.h"

#include "shell.h"

/*
 * Handler de los eventos de svc_wifi. Va antes de registrar la app: sin esto la
 * pantalla no se entera de los scans ni de las conexiones.
 */
esp_err_t wifi_scan_ui_init(void);

/* Se registra desde app_main.c. */
extern const os_app_t app_wifi;

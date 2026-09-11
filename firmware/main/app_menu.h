#pragma once

#include "esp_err.h"

/*
 * Menu de apps: un carrusel sobre la pantalla actual, en lv_layer_top().
 * UI_EVENT_MENU (click en BOOT) lo abre y lo cierra.
 *
 * Registra su handler en el loop de eventos por defecto, que tiene que existir.
 */
esp_err_t app_menu_init(void);
void app_menu_close(void);

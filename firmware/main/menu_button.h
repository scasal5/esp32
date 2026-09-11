#pragma once

#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Eventos de la UI, en el loop de eventos por defecto. */
ESP_EVENT_DECLARE_BASE(UI_EVENT);

typedef enum {
    UI_EVENT_MENU,      /* click en BOOT: abrir o cerrar el menu */
} ui_event_id_t;

/*
 * Boton BOOT (GPIO0) como boton de menu: un click publica UI_EVENT_MENU.
 * El loop de eventos por defecto tiene que existir antes.
 */
esp_err_t menu_button_start(void);

#ifdef __cplusplus
}
#endif

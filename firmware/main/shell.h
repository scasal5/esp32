#pragma once

/*
 * Shell: navegacion y ciclo de vida de las apps.
 *
 * El contrato esta en docs/arquitectura.md. Lo que importa desde el lado de una
 * app: recibe un `root` ya creado, cuelga todo de ahi y no sabe quien la
 * presenta. No conoce al lanzador, ni la capa de LVGL en la que vive, ni al
 * boton BOOT.
 */

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "lvgl.h"

typedef struct {
    const char *id;                /* "wifi": prefijo de logs y clave en NVS */
    const char *icon;              /* simbolo LVGL para la tarjeta */
    const char *name;              /* texto visible en el lanzador */

    /* Construye la UI dentro de root. El shell lo llama desde la task de LVGL,
       con el lock tomado. NULL: la tarjeta aparece pero no abre nada todavia. */
    void (*open)(lv_obj_t *root);

    /* Libera lo que no cuelga de root, sobre todo los lv_timer. El root lo
       borra el shell justo despues. */
    void (*close)(void);
} os_app_t;

/*
 * Registro explicito, desde app_main.c y detras de su opcion de Kconfig. El
 * shell guarda el puntero: la estructura tiene que vivir todo el programa.
 */
void shell_register_app(const os_app_t *app);

/* Escucha UI_EVENT_MENU (BOOT). Sin esto no hay navegacion. */
esp_err_t shell_init(void);

/*
 * Una app pide volver a la pantalla de inicio; es lo mismo que apretar BOOT.
 * El cierre se agenda con lv_async_call, asi que se puede llamar desde el
 * evento de un objeto que el cierre va a borrar.
 */
void shell_close_app(void);

/*
 * Mientras claim=true, UI_EVENT_MENU (BOOT) no navega: la app activa lo usa
 * para si misma (p.ej. flap). Hay que soltarlo en close() o el shell queda
 * sordo al boton.
 */
void shell_claim_boot(bool claim);

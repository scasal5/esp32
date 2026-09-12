#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Arranque de la pantalla de inicio.
 *
 * Una task propia monta assets y busca fondo. Hay un solo archivo a la vez:
 * lo que se sube reemplaza lo anterior, y splash_gif_clear() deja la home
 * sin imagen. Si una placa vieja todavia tiene los dos, gana el PNG y se
 * borra el GIF (o al reves si el PNG no carga).
 *
 *   - BSP_SPIFFS_MOUNT_POINT/splash.png: se decodifica y se convierte en un
 *     fondo estatico, mas chico, desenfocado y oscurecido;
 *   - BSP_SPIFFS_MOUNT_POINT/splash.gif: se lee entero a PSRAM y queda como
 *     fondo animado a tamano completo, en RGB565 y en loop. El archivo y el
 *     objeto viven mientras el fondo exista: lv_gif no copia el buffer.
 *
 * Despues reemplaza el splash de texto por la pantalla de inicio (fondo + hora
 * + bateria). Sin fondo usable la pantalla de inicio sale igual, sin fondo.
 * Llamar despues de encender el backlight.
 */
void splash_gif_start(void);

/* PNG estatico o GIF animado 240x284. Lo pone de fondo, lo guarda y borra
   el otro archivo. */
esp_err_t splash_gif_install(const uint8_t *data, size_t len);

/*
 * Quita el fondo: borra splash.gif y splash.png, libera el buffer y deja la
 * home sin imagen. Lock de LVGL tomado.
 */
esp_err_t splash_gif_clear(void);

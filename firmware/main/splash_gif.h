#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Arranque de la pantalla de inicio.
 *
 * Una task propia monta assets, lee BSP_SPIFFS_MOUNT_POINT/splash.gif a PSRAM,
 * decodifica el frame 0 y lo convierte una sola vez en fondo: mas chico,
 * desenfocado y oscurecido. Despues reemplaza el splash de texto por la
 * pantalla de inicio (fondo + hora + bateria). Sin GIF usable, la pantalla de
 * inicio sale igual, sin fondo. Llamar despues de encender el backlight.
 */
void splash_gif_start(void);

/* PNG estatico o GIF animado 240x284. Lo pone de fondo y lo guarda. */
esp_err_t splash_gif_install(const uint8_t *data, size_t len);

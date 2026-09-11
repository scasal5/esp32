#pragma once

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

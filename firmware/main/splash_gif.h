#pragma once

/*
 * Fase 1a: primer frame de BSP_SPIFFS_MOUNT_POINT/splash.gif sobre el splash
 * de texto.
 *
 * Arranca una task propia: monta assets, lee el GIF a PSRAM, valida la
 * cabecera y muestra el frame 0 en pausa. Cualquier falla deja el splash de
 * texto como esta. Llamar despues de encender el backlight.
 */
void splash_gif_start(void);

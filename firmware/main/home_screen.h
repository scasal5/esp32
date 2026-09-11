#pragma once

#include "lvgl.h"

/*
 * Pantalla de inicio: fondo opcional con la hora (UTC-3) y la bateria encima,
 * actualizadas en vivo.
 *
 * bg: imagen ARGB8888 ya procesada, o NULL para mostrar solo hora y bateria.
 * Pasa a ser de la pantalla y vive mientras ella exista.
 *
 * Llamar con bsp_display_lock() tomado.
 */
void home_screen_show(lv_draw_buf_t *bg);

#pragma once

#include "lvgl.h"

/*
 * Pantalla de inicio: fondo opcional y barra de estado (hora UTC-3, WiFi si
 * hay IP, bateria, USB si hay VBUS).
 *
 * bg: imagen ARGB8888 ya procesada, o NULL para mostrar solo la barra.
 * Pasa a ser de la pantalla y vive mientras ella exista.
 *
 * Llamar con bsp_display_lock() tomado.
 */
void home_screen_show(lv_draw_buf_t *bg);

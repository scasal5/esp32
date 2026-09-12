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

/* Reemplaza el fondo. El buf anterior se destruye. Lock de LVGL tomado. */
void home_screen_set_bg(lv_draw_buf_t *bg);

/* GIF animado como fondo. Lock de LVGL tomado. */
void home_screen_set_gif(lv_obj_t *gif);

/*
 * Frena o reanuda el fondo animado. Lo llama el shell cuando algo tapa la
 * pantalla de inicio: el GIF cuesta lo mismo tapado que a la vista. El estado
 * se guarda aunque no haya GIF, para que un fondo subido con Fondo abierta
 * nazca en pausa. Lock de LVGL tomado.
 */
void home_screen_pause_bg(bool paused);

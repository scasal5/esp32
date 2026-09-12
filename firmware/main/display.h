#pragma once

#include "lvgl.h"

/*
 * Arranque del panel, en reemplazo de bsp_display_start().
 *
 * El BSP hace lo mismo pero con tres decisiones que cuestan caro en una
 * pantalla que se repinta entera 14 veces por segundo: un solo buffer, sin
 * DMA, y max_transfer_sz en cero (el default de 4092 bytes parte cada tira en
 * una docena de transacciones). Aca se arma igual, con los mismos pines
 * publicos del BSP y la misma secuencia del ST7789, pero con doble buffer en
 * RAM interna DMA y el bus configurado para la tira completa.
 *
 * Todo lo demas del BSP se sigue usando: el tactil, el backlight y
 * bsp_display_lock() / unlock(), que envuelven a lvgl_port.
 *
 * Devuelve NULL si el panel no arranco.
 */
lv_display_t *display_start(void);

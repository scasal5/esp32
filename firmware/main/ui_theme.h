#pragma once

/*
 * Tokens visuales de ws183-os. Un solo lugar para el color y la tipografia de
 * todas las pantallas: splash, inicio, carrusel y WiFi.
 *
 * Es la misma escala que usan los SVG de docs/brand: fondo casi
 * negro, tres niveles de gris para el texto y superficies apenas mas claras que
 * el fondo. Sin color de acento: en esta pantalla el color se reserva para
 * cuando signifique algo.
 *
 * Las fuentes son las Montserrat que habilita sdkconfig.defaults. Cambiar una
 * fuente aca obliga a habilitar su CONFIG_LV_FONT_MONTSERRAT_* o no linkea.
 */

#include "lvgl.h"

/* --- Color ------------------------------------------------------------- */

/* Fondo de pantalla completa. */
#define UI_COL_BG          lv_color_hex(0x0A0A0A)

/* Tarjetas, listas y campos: apenas por encima del fondo, sin borde. */
#define UI_COL_SURFACE     lv_color_hex(0x161616)

/* Separadores. Casi invisibles a proposito. */
#define UI_COL_HAIRLINE    lv_color_hex(0x2A2A2A)

/* Texto. Tres niveles, todos legibles sobre UI_COL_BG:
   PRIMARY el dato, SECONDARY lo que acompana, MUTED la etiqueta. */
#define UI_COL_TEXT        lv_color_hex(0xFAFAFA)
#define UI_COL_TEXT_2      lv_color_hex(0xB4B4B4)
#define UI_COL_TEXT_MUTED  lv_color_hex(0x8A8A8A)

/* El QR necesita contraste real para que lo lea una camara: blanco puro sobre
   el fondo de la pantalla, sin grises intermedios. */
#define UI_COL_QR_LIGHT    lv_color_hex(0xFFFFFF)
#define UI_COL_QR_DARK     UI_COL_BG

/* --- Tipografia -------------------------------------------------------- */

/* Un dato que se lee de lejos: la hora, el nombre en el splash. */
#define UI_FONT_DISPLAY    &lv_font_montserrat_36

/* Titulo de pantalla y nombre de app. */
#define UI_FONT_TITLE      &lv_font_montserrat_20

/* Todo lo demas. */
#define UI_FONT_BODY       &lv_font_montserrat_14

/* --- Movimiento -------------------------------------------------------- */

/*
 * Una sola duracion y una sola curva para todas las transiciones. Las anima el
 * shell; ninguna app llama a lv_anim por su cuenta. Si manana se siente lento,
 * se cambia un numero y cambia todo el sistema.
 */
#define UI_MOTION_MS       180
#define UI_MOTION_PATH     lv_anim_path_ease_in_out
#define UI_FADE_FROM       LV_OPA_0
#define UI_FADE_TO         LV_OPA_COVER

/* --- Ritmo ------------------------------------------------------------- */

/* Margen lateral unico. La pantalla mide 240 px: mas que esto come contenido. */
#define UI_PAD_SIDE        10

/* Radio de tarjetas y listas. */
#define UI_RADIUS          16

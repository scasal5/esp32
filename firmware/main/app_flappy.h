#pragma once

#include "shell.h"

/*
 * Flappy Bird: clon jugable en el lanzador (id "flappy").
 *
 * Graficos: Kenney Tappy Plane (CC0) embebidos como LVGL RGB565A8
 * (ave 34x28, tubo 52x115, suelo tile 48x71). Cielo = color solido.
 * SFX: Kenney Digital Audio (CC0) via flappy_sfx_play() stub (sin codec
 * path aun). Ver assets/flappy/NOTICE. No se usan packs propietarios
 * (samuelcust / Sounds Resource / etc.). MegaCrash itch CC0 pendiente
 * de zip legal (blocker documentado en NOTICE).
 */
extern const os_app_t app_flappy;

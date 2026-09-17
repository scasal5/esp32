#pragma once

#include "shell.h"

/*
 * Flappy Bird: clon jugable en el launcher (id "flappy").
 *
 * Graficos: Yorokobi flappy_atlas (CC0) + grass_dirt (CC0), cielo #4EC0CA.
 * SFX: Kenney Digital Audio (CC0) via flappy_sfx_play() stub.
 * Salida: doble short-press del PWR (GPIO41 SYS_OUT). Sin boton Salir on-screen.
 * Ver assets/flappy/NOTICE.
 */
extern const os_app_t app_flappy;

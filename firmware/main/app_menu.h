#pragma once

/*
 * Lanzador: el carrusel de tarjetas. Es solo la vista del registro de apps; que
 * pasa al tocar una tarjeta lo decide el shell.
 */

#include <stdbool.h>
#include <stddef.h>

#include "shell.h"

/*
 * Dibuja el carrusel con las apps registradas. `on_pick` se llama ya agendado
 * en la task de LVGL, asi que puede borrar el lanzador. Las apps sin `open`
 * no llaman a `on_pick`: la tarjeta avisa que la app todavia no existe.
 */
void app_menu_show(const os_app_t *const *apps, size_t count,
                   void (*on_pick)(const os_app_t *app));

void app_menu_hide(void);

bool app_menu_visible(void);

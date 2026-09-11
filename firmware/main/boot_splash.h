#pragma once

/* Pantalla de carga. Se dibuja con el backlight todavia apagado; quien
   llama enciende la luz despues, para que el primer frame visible ya sea
   el splash y no el contenido sucio del framebuffer. */
void boot_splash_show(void);

/* Oculta el titulo y la bateria del splash de texto y borra su timer de
   bateria (la pantalla de inicio lee la bateria por su cuenta). La llama la
   task de la imagen de inicio justo antes de mostrar la pantalla de inicio.
   Toma el lock de LVGL (recursivo): se puede llamar con el lock ya tomado. */
void boot_splash_hide_text(void);

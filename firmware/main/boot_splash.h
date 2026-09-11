#pragma once

/* Pantalla de carga. Se dibuja con el backlight todavia apagado; quien
   llama enciende la luz despues, para que el primer frame visible ya sea
   el splash y no el contenido sucio del framebuffer. */
void boot_splash_show(void);

/* Oculta el titulo y el estado de bateria del splash de texto. La llama la
   task del GIF cuando el frame 0 ya esta en pantalla; si el GIF no carga, no
   se llama y el texto queda. Toma el lock de LVGL (recursivo): se puede
   llamar con el lock ya tomado. */
void boot_splash_hide_text(void);

#pragma once

/* Pantalla de carga. Se dibuja con el backlight todavia apagado; quien
   llama enciende la luz despues, para que el primer frame visible ya sea
   el splash y no el contenido sucio del framebuffer. */
void boot_splash_show(void);

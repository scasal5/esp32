# Arquitectura

> **Diseno propuesto.** Hoy solo existen el servicio de bateria
> (`main/pm_axp2101.cpp`) y el splash (`main/boot_splash.c`). Este documento
> fija las reglas antes de escribir el resto, para que las contribuciones
> encajen sin reescrituras. Si algo no cierra, se discute en un issue antes de
> programarlo.

## Capas

```
apps        fondo, wifi, ajustes ...           solo UI; no tocan hardware
  |
shell       inicio, gestos, barra de estado    navegacion y registro de apps
  |
servicios   pm, storage, wifi, time            hardware y estado; sin LVGL
  |
BSP         waveshare/esp32_s3_touch_lcd_1_83 + ESP-IDF 5.5
```

Cada capa solo conoce a la de abajo.

## Reglas

1. **Los servicios no incluyen `lvgl.h`.** Exponen una API en C y publican sus
   cambios por `esp_event`. Un servicio tiene que poder probarse sin pantalla.
2. **Las apps no incluyen drivers.** Del BSP solo usan `bsp_display_lock()` y
   `bsp_display_unlock()`. El resto lo piden a un servicio.
3. **LVGL se usa desde un solo lugar a la vez.**
   - Dentro de un `lv_timer` o de un evento de objeto LVGL, el codigo ya corre
     en la task de LVGL con el lock tomado: no hay que pedirlo.
   - Desde cualquier otra task, incluido un handler de `esp_event`, se llama a
     LVGL solo entre `bsp_display_lock()` y `bsp_display_unlock()`.
4. **Nada bloquea el primer frame.** El orden de `app_main` se mantiene: I2C,
   PMU, panel, splash, backlight. Montar `assets`, la microSD o levantar WiFi
   viene despues, y en su propia task si puede tardar.
5. **Nada opcional rompe el arranque.** Sin `assets`, sin microSD, sin red o
   sin PMU, la placa arranca y muestra el splash de texto.
6. **Todo lo opcional se apaga por Kconfig**, en un menu `ws183-os` de
   `idf.py menuconfig`. El build tiene que pasar con todas las apps apagadas.

## Arbol propuesto

```
firmware/
├── main/
│   ├── app_main.c          orden de arranque; registra servicios y apps
│   └── Kconfig.projbuild   menu ws183-os
├── components/
│   ├── svc_pm/             AXP2101 (hoy main/pm_axp2101.cpp)
│   ├── svc_storage/        monta assets y microSD
│   ├── svc_wifi/           escaneo, conexion y redes guardadas
│   ├── svc_time/           SNTP + RTC
│   ├── shell/              inicio, gestos, barra de estado, registro de apps
│   ├── app_fondo/          elegir, encuadrar y dibujar la imagen de inicio
│   └── app_wifi/           lista de redes y conexion
└── assets/                 contenido de la particion assets
```

Los prefijos `svc_` y `app_` dicen a que capa pertenece cada componente con solo
leer el nombre. Mover `pm` a `components/svc_pm/` es el primer paso de la fase 2,
no un requisito para la fase 1.

## Contrato de una app

```c
typedef struct {
    const char *id;                /* "fondo": clave en NVS y prefijo de logs */
    const char *name;              /* texto visible en el shell */
    void (*open)(lv_obj_t *root);  /* construye la UI dentro de root */
    void (*close)(void);           /* libera lo que no cuelga de root */
} os_app_t;

void shell_register_app(const os_app_t *app);
```

- El registro es explicito, desde `app_main.c` y detras de su opcion de
  Kconfig. Sin trucos de linker: quien lee `app_main.c` ve todo lo que arranca.
- El shell llama a `open` y `close` desde la task de LVGL, con el lock tomado.
- Todo objeto que crea la app cuelga de `root`, y el shell borra `root` al
  salir. Los `lv_timer` no dependen de ningun objeto: la app los guarda y los
  borra en `close`, o siguen corriendo contra objetos que ya no existen.

## Servicios y eventos

```c
ESP_EVENT_DECLARE_BASE(SVC_PM_EVENT);

typedef enum {
    SVC_PM_EVENT_STATUS,           /* payload: pm_status_t */
} svc_pm_event_t;
```

Hoy `boot_splash.c` lee el AXP2101 por I2C desde un `lv_timer`, o sea dentro de
la task de LVGL. Para v0 alcanza. Con mas servicios, una transaccion I2C lenta
frena el render. En la fase 2, `svc_pm` sondea desde su propia task y publica
`SVC_PM_EVENT_STATUS`; la barra de estado lo escucha y actualiza la etiqueta
entre lock y unlock (regla 3).

Para WiFi, ESP-IDF ya publica `WIFI_EVENT` e `IP_EVENT`. `svc_wifi` solo agrega
la lista de redes del ultimo escaneo y las redes guardadas. Las credenciales
viven en NVS y nunca en el repo.

## Imagen de inicio

**Decision:** la imagen de inicio es un GIF, `/spiffs/splash.gif`, reproducido
con `lv_gif` de LVGL. Quien quiera cambiarla reemplaza un archivo; no hay
conversor en el camino. Una imagen estatica es un GIF de un solo frame.

Todo lo que sigue se verifico contra LVGL 9.5.0 y esp_lvgl_port 2.9.0, las
versiones de `dependencies.lock`.

### Reglas del archivo

| | Limite | Por que |
|---|---|---|
| Formato | `GIF87a` o `GIF89a` | lo que abre `lv_gif` |
| Ancho | <= 240 px | es el ancho del panel. `lv_gif` acepta hasta 480 (`MAX_WIDTH` en `AnimatedGIF.h`) pero no se escala en la placa: reescalar cada frame en software cuesta CPU |
| Alto | <= 284 px | es el alto del panel |
| Tamano del archivo | <= 1 MB (propuesto, ajustable por Kconfig) | el archivo entero se carga en PSRAM |
| Presencia | opcional | si falta o no valida, queda el splash de texto (regla 5) |

Comportamiento heredado de AnimatedGIF, la libreria que usa `lv_gif`:

- Un frame con delay de 0 o 1 centesima se muestra 100 ms (`gif.c`).
- Sin bloque `NETSCAPE2.0`, la animacion se reproduce una sola vez y se
  detiene. Con loop 0 se repite siempre.
- La transparencia se pinta con el color de fondo del GIF: el framebuffer es
  RGB565 y no mezcla con lo que haya debajo.

### Configuracion necesaria

- `CONFIG_LV_USE_CLIB_MALLOC=y`. Hoy rige `CONFIG_LV_USE_BUILTIN_MALLOC=y` con
  `CONFIG_LV_MEM_SIZE_KILOBYTES=64` y sin expansion: el objeto `lv_gif` ya
  incluye el estado del decodificador (`GIFIMAGE`, unos 24 KB) y el framebuffer
  no entra. Con el malloc de C y `CONFIG_SPIRAM_USE_MALLOC=y`, los pedidos de
  mas de 16 KB (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`) se sirven primero desde
  PSRAM.
- `CONFIG_LV_USE_GIF=y`.
- `bsp_spiffs_mount()` para tener `/spiffs`. No hace falta un driver de archivos
  de LVGL: el GIF se lee con `fopen` y se entrega en memoria.
- `CONFIG_BSP_SPIFFS_FORMAT_ON_MOUNT_FAIL` queda en `n`. En una placa que nunca
  recibio `assets-flash`, la zona de `assets` tiene restos del firmware de
  fabrica; formatearla en silencio borraria sin avisar lo que el usuario haya
  grabado si el mount falla por otra causa.

### Memoria

| | Maximo | Donde |
|---|---|---|
| Archivo GIF | limite de tamano (1 MB) | PSRAM; vive mientras exista el objeto |
| Framebuffer de `lv_gif` | 240x284x2 = 136 KB en RGB565 | PSRAM |
| Estado de AnimatedGIF | unos 24 KB, dentro del objeto | PSRAM |

El framebuffer se pide en `LV_COLOR_FORMAT_RGB565` con
`lv_gif_set_color_format()` **antes** de `lv_gif_set_src()`. El default es
ARGB8888, que duplica la memoria (272 KB) sin ganar nada en este panel.

### Fase 1a: primer frame estatico

1. `app_main` dibuja el splash de texto y enciende la luz, igual que hoy. No se
   agrega nada antes del primer frame (regla 4).
2. Una task propia, **fuera** del lock de LVGL:
   - `bsp_spiffs_mount()`; si falla, termina y queda el splash de texto;
   - `stat()` de `/spiffs/splash.gif` y control del limite de tamano;
   - lectura completa a un buffer en PSRAM;
   - validacion de la cabecera a mano: firma de 6 bytes y ancho y alto en los
     bytes 6..9, little-endian, dentro de los limites.
3. Con `bsp_display_lock()` tomado:
   - `lv_gif_create()` y `lv_gif_set_color_format(gif, LV_COLOR_FORMAT_RGB565)`;
   - `lv_gif_set_src(gif, &dsc)`, con un `lv_image_dsc_t` que apunta al buffer.
     `lv_gif` guarda los punteros sin copiar: `dsc` y el buffer tienen que vivir
     tanto como el objeto, asi que `dsc` no puede estar en el stack. Esta
     llamada **decodifica el frame 0 y ademas arranca el timer** de la
     animacion;
   - `lv_gif_pause(gif)` en la misma seccion bloqueada: el timer no llega a
     correr;
   - si `lv_gif_is_loaded(gif)` es falso: borrar el objeto, liberar el buffer y
     seguir con el splash de texto;
   - si cargo: ocultar el splash de texto.

La decodificacion del frame 0 ocurre con el lock tomado y frena el render
mientras dura. Hay que medirla en la fase 1a, no solo en la 1b.

Evitar:

- **`lv_gif_get_size()`:** declara un `GIFIMAGE` de unos 24 KB en el stack de
  quien lo llama (la task de LVGL tiene 7168 B, `ESP_LVGL_PORT_INIT_CONFIG`) y
  abre el archivo por el sistema de archivos de LVGL. La cabecera se valida a
  mano.
- **Rutas de LVGL (`"S:/splash.gif"`):** `lv_gif` leeria la flash en cada frame,
  dentro de la task de LVGL.

### Fase 1b: animacion

- La animacion arranca con `lv_gif_resume()` cuando el arranque termino. **No
  con `lv_gif_restart()`:** fuerza `loop_count = -1`, y la animacion se detiene
  al completar una vuelta.
- Antes de dejarla activa por defecto se mide en hardware: tiempo por frame de
  `GIF_playFrame` (corre en la task de LVGL, asi que un frame lento demora el
  tactil), PSRAM libre antes y despues
  (`heap_caps_get_free_size(MALLOC_CAP_SPIRAM)`) y cuadros por segundo reales.
- **Al cambiar de pantalla, el objeto se borra** con `lv_obj_delete()`. Su
  destructor cierra el decodificador, libera el framebuffer y borra el timer.
  El buffer del archivo es nuestro: lo libera un handler de `LV_EVENT_DELETE`.
- `lv_gif_set_auto_pause_invisible(gif, true)` sirve solo de red de seguridad:
  pausa si la pantalla del GIF no es la activa, pero **no reanuda sola** y no
  libera memoria.

### Guardado seguro

Aplica cuando la placa escribe `splash.gif`: subida desde el celular (fase 5) o
copia desde la microSD. En SPIFFS, `rename()` **falla si el destino ya existe**
(`SPIFFS_ERR_CONFLICTING_NAME` en `SPIFFS_rename`), asi que el truco de escribir
un temporal y renombrarlo encima no alcanza:

1. se escribe `splash.new` completo, se cierra y se valida con las reglas de
   arriba;
2. se borra `splash.gif`;
3. se renombra `splash.new` a `splash.gif`.

Al arrancar, si falta `splash.gif` pero existe un `splash.new` valido, se
termina el paso 3. Un corte de energia en cualquier punto deja el GIF viejo, el
nuevo o el splash de texto, nunca un archivo a medias.

### Consecuencias para la fase 3

La placa no tiene codificador de GIF, asi que la app *Fondo* **no reescribe el
archivo**:

- encuadrar es guardar un desplazamiento en NVS y aplicarlo a la posicion del
  objeto;
- dibujar encima es una capa aparte, guardada en su propio archivo;
- el zoom usaria `lv_image_set_scale()`, que escala cada frame en software.
  Queda sujeto a la medicion de la fase 1b.

### Grabado desde la PC

`idf.py -p COM3 assets-flash`. Ese target **no existe hasta que la fase 1a
declare** `spiffs_create_partition_image(assets assets)` en `CMakeLists.txt`,
sin `FLASH_IN_PROJECT`, para que `idf.py flash` no pise lo grabado en la placa
(ver [README](../README.md#imagen-de-inicio)).

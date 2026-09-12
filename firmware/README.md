# ws183-os

Un mini sistema operativo **abierto y modificable** para la **Waveshare
ESP32-S3-Touch-LCD-1.83**: pantalla de inicio con imagen propia, gestos, WiFi y
apps chicas. Construido sobre ESP-IDF oficial y el BSP publicado por Waveshare,
no es un fork del firmware de fabrica.

La idea es que cualquiera pueda clonar el repo, cambiar la imagen o los colores,
agregar una app y flashearlo en su placa.

## Estado

**v0.** Arranca el panel y muestra un splash con el estado de la bateria. Todo
lo que este documento marca como *planeado* todavia no existe.

| | Hoy |
|---|---|
| Panel ST7789 y backlight sin parpadeo al arrancar | funciona |
| Bateria (AXP2101, solo lectura) | funciona |
| Tactil CST816S | el BSP lo registra en LVGL, pero nada lo usa todavia |
| WiFi: scan y lista de redes cercanas | funciona |
| Imagen personalizable, gestos y apps | planeado |

## Hacia donde va

El splash deja de ser un texto fijo y pasa a ser la **pantalla de inicio**:

```
┌────────────────────┐
│ 12:40:31   WiFi*   87% │  barra de estado
│                   USB* │
│                    │
│    [ tu imagen ]   │  imagen elegida por el usuario
│                    │
│                    │
└────────────────────┘
  boton de arriba -> menu
  boton de abajo  -> apagar
  Icono de WiFi SOLO si esta conectado*
  USB si esta conectado*
```

BOOT (arriba) abre el menu. PWR (abajo) apaga por el AXP2101: el firmware
no toca ese pin. WiFi en la barra solo aparece con IP; USB, si hay VBUS.
Los gestos siguen siendo una propuesta.

### Hoja de ruta

| Fase | Que | Estado |
|---|---|---|
| 0 | Panel, splash y bateria en solo lectura | hecho |
| 1a | Imagen de inicio: primer frame de `splash.gif` desde `assets`, con el splash actual como respaldo | planeado |
| 1b | Animacion de `splash.gif`, con decodificacion y PSRAM medidas en hardware | planeado |
| 2 | Shell: gestos, barra de estado y registro de apps | planeado |
| 3 | App *Fondo*: elegir imagen, encuadrarla y dibujar encima | planeado |
| 4 | WiFi: scan, QR+SoftAP para la clave, recordar en NVS | hecho |
| 5 | Subir GIFs desde el celular por la red local | planeado |
| 6 | Hora por SNTP + RTC; actualizaciones OTA a `ota_0` | planeado |
| — | CI: cada PR corre `idf.py build` con ESP-IDF 5.5.1 contra `firmware/` | hecho |

Mas adelante, sin orden fijo: IMU (girar la imagen, despertar al levantar),
audio, microSD y gestion de energia. La gestion de energia va ultima a
proposito: el light sleep es lo que colgaba el USB en el firmware de fabrica
(ver [Si la placa no responde por USB](#si-la-placa-no-responde-por-usb)).

La imagen de inicio es **un GIF**, animado o de un solo frame: para cambiarla
alcanza con reemplazar un archivo, sin conversores. En una pantalla de 1,83"
editar sirve para encuadrar y retocar. Los GIFs nuevos van a entrar por
`firmware/assets/` al compilar (fase 1a), por la microSD o desde el celular
(fase 5).

Capas, reglas de hilos y el contrato de una app estan en
[`docs/arquitectura.md`](docs/arquitectura.md).

## Hardware

| | |
|---|---|
| SoC | ESP32-S3 rev v0.2, QFN56, dual-core LX7 @ 240 MHz |
| PSRAM | 8 MB octal, AP Memory 3V3 @ 80 MHz |
| Flash | 16 MB externa (JEDEC `0x20` / `0x4018`) |
| Pantalla | 240 x 284, driver `esp_lcd_panel_st7789` por SPI |
| Tactil | CST816S (I2C) |
| IMU | QMI8658 (I2C) |
| Audio | codec ES8311 + ADC de microfonos ES7210, I2S |
| PMU | AXP2101 (I2C) |
| Almacenamiento | ranura microSD |

Detalle completo y verificado en [`docs/hardware.md`](docs/hardware.md).

## Estado de bateria

El AXP2101 se inicializa en modo **solo lectura** despues del bus I2C. El
firmware habilita los ADC de medicion, pero no cambia tensiones, rieles,
carga ni apagado. Si el PMU no responde, el arranque continua y el splash
muestra `sin PMU`.

El splash lee el estado inmediatamente y lo refresca cada 3 segundos:

- `100%` o `98% USB` cuando hay porcentaje disponible;
- la tension medida, por ejemplo `3870 mV`, si el PMU no puede estimar el
  porcentaje;
- `sin bateria` cuando no hay una bateria conectada.

Tambien se registra una linea de telemetria con presencia, carga, porcentaje y
las tensiones de bateria, VBUS y sistema. Todavia no hay logica de carga ni de
apagado.

## WiFi

El servicio inicia WiFi en modo STA despues del primer frame. Al abrir la card
**WiFi** del carrusel, ejecuta un scan asincrono y muestra hasta 16 redes
cercanas, ordenadas por RSSI. Los SSID repetidos se consolidan y se conserva
la senal mas fuerte; las protegidas llevan `*`. Las ocultas no se listan.

Tocar una red abierta no conecta: solo se aceptan redes con clave. Tocar una
red con `*` abre un SoftAP `ws183-XXXX` y un QR `WIFI:T:nopass;S:ws183-XXXX;;`.
El celular se une al AP, el captive portal pide la contrasena (el SSID ya esta
elegido) y la placa conecta como STA. La clave se guarda en NVS, namespace
`wifi`; no se loguea. Al conseguir IP, el SoftAP se apaga y la barra de inicio
muestra **WiFi**. Cerrar el QR sin escanear no deja el STA asociado.

BOOT o **Cerrar** corta el portal y vuelve al inicio. Si WiFi no pudo iniciar,
la pantalla muestra `WiFi no listo` y el resto del firmware sigue funcionando.

Por USB: `wifi`, `wifiscan`, `wifiprov <ssid>`, `wificonnect <ssid> <pass>`,
`wifidisconnect`, `wifistop`.

## Compilar

Requiere **ESP-IDF 5.5.x oficial**. No uses el arbol del vendedor: el firmware
de fabrica se compilo con un IDF marcado `-dirty`, que no es reproducible.

```
idf.py set-target esp32s3
idf.py build
```

Cada PR y cada push a `main` corren ese mismo `idf.py build` en GitHub Actions
([`.github/workflows/build.yml`](../.github/workflows/build.yml)), con
ESP-IDF 5.5.1 fijo y target `esp32s3`, contra `firmware/`. Parte de un clone
limpio: sin `sdkconfig` local, solo `sdkconfig.defaults` y `dependencies.lock`.
No flashea ni prueba en placa; si una dependencia no resuelve o el build se
rompe, el PR queda en rojo antes de llegar a COM3.

El Component Manager baja el BSP y sus dependencias en el primer build. El
manifiesto vive en [`main/idf_component.yml`](main/idf_component.yml): pertenece
al componente `main`, no a la raiz del proyecto. Si se lo pone al lado de
`main/`, el Component Manager lo ignora en silencio y el build falla mas tarde
con headers que no aparecen.

Versiones resueltas en el primer build verde:

| Componente | Version |
|---|---|
| `waveshare/esp32_s3_touch_lcd_1_83` | 2.0.0 |
| `lvgl/lvgl` | 9.5.0 |
| `espressif/esp_lvgl_port` | 2.9.0 |
| `espressif/esp_codec_dev` | 1.5.11 |
| `espressif/esp_lcd_touch_cst816s` | 1.1.2 |
| `espressif/esp_lcd_panel_io_additions` | 1.0.1 |

El pin `lvgl/lvgl: "^9"` no es opcional: el BSP declara `>=8,<10`, y con LVGL 8
no existen `lv_display_t` ni `lv_screen_active()`.

## Nota sobre esptool

ESP-IDF 5.5 incluye **esptool 4.12**. Los subcomandos con guion
(`write-flash`, `read-flash`, `chip-id`) solo existen en esptool 5.x. Este repo
usa siempre la forma con guion bajo (`write_flash`, `read_flash`, `chip_id`),
que funciona en las dos.

## Flashear

> El primer `idf.py flash` **sobrescribe bootloader, tabla de particiones y
> `factory`**. Hace falta tener el backup de fabrica antes de correrlo.

```
idf.py -p COM3 flash monitor
```

Esperado en el monitor: log por USB-Serial-JTAG, `cpu freq: 240000000 Hz`,
`panel up: 240x284`, una linea `AXP2101 inicializado` y el splash con el estado
de bateria. Si el log vive pero la pantalla queda negra, el problema es
backlight o PMU, no el USB.

El firmware **se queda en el splash**: `app_main` retorna despues de dibujarlo
y la task de LVGL mantiene la pantalla y actualiza la bateria. No es un
cuelgue, es el estado final de v0.

El warning `ledc: GPIO 40 is not usable, maybe conflict with others` es
cosmetico: el backlight enciende igual.

El warning `i2c.master: Please check pull-up resistances whether be connected
properly` es del mismo tipo. ESP-IDF lo imprime siempre que se crea un bus I2C
sin la pull-up interna (`esp_driver_i2c/i2c_master.c`), y el BSP no la activa.
No indica un bus colgado: el AXP2101 contesta en ese mismo bus justo despues
(`AXP2101 inicializado` y la linea de telemetria).

Despues de `splash visible` se espera tambien `wifi: STA up`. Para probar el
scan, pulsa BOOT, abre la card **WiFi** y espera la lista de SSID con su RSSI.
Toca una red con clave: aparece el QR. En el celular, escanea, unete al AP
`ws183-XXXX`, pone la contrasena y volve a la placa. Otro click de BOOT, o
**Cerrar**, vuelve al inicio.

## Personalizar

Lo que se puede cambiar hoy, sin tocar el resto del firmware:

| Que | Donde |
|---|---|
| Titulo y colores del splash | `boot_splash_show()` en [`main/boot_splash.c`](main/boot_splash.c) |
| Periodo de refresco de la bateria | `BATTERY_REFRESH_MS` en el mismo archivo |
| Brillo al arrancar | `bsp_display_brightness_set(80)` en [`main/app_main.c`](main/app_main.c) |
| Fuentes disponibles | `CONFIG_LV_FONT_MONTSERRAT_*` en [`sdkconfig.defaults`](sdkconfig.defaults) |

### Imagen de inicio (planeado, fases 1a y 1b)

> **Nada de esto existe todavia.** No hay carpeta `firmware/assets/`, el
> [`CMakeLists.txt`](CMakeLists.txt) no declara ninguna imagen SPIFFS, LVGL no
> tiene habilitado el GIF y el comando `assets-flash` de abajo **falla hoy**. Se
> documenta el diseno para que la fase 1a lo implemente tal cual.

La imagen es `firmware/assets/splash.gif`:

| | Regla |
|---|---|
| Formato | GIF, animado o de un frame |
| Tamano | hasta **240 px de ancho** y **284 px de alto**; no se escala en la placa |
| Opcional | si falta o no es valido, se ve el splash de texto de siempre |

Detalle de limites, memoria y secuencia de carga en
[`docs/arquitectura.md`](docs/arquitectura.md#imagen-de-inicio).

La fase 1a agrega estas cosas juntas:

1. la carpeta `firmware/assets/`, con `splash.gif` por defecto;
2. en [`CMakeLists.txt`](CMakeLists.txt), despues de `project()`:

   ```cmake
   spiffs_create_partition_image(assets assets)
   ```

   **Sin `FLASH_IN_PROJECT`**, a proposito: asi `idf.py flash` no graba
   `assets` y un flash de desarrollo no borra las imagenes editadas en la
   placa;
3. en [`sdkconfig.defaults`](sdkconfig.defaults): `CONFIG_LV_USE_CLIB_MALLOC=y`
   y `CONFIG_LV_USE_GIF=y`. Hoy LVGL usa un pool propio de 64 KB, donde no entra
   ni el estado del decodificador;
4. montar `assets` y mostrar el **primer frame** de `splash.gif` despues del
   splash de texto, sin demorar el primer frame. La animacion queda para la
   fase 1b.

Con eso declarado, ESP-IDF 5.5 genera el target `assets-flash`, que graba solo
esa particion cuando uno lo pide:

```
idf.py -p COM3 assets-flash
```

El target lo crea `spiffs_create_partition_image` en
`components/spiffs/project_include.cmake` de ESP-IDF, lleve o no
`FLASH_IN_PROJECT`. Sin esa llamada en el CMake del proyecto, no existe. La
ruta `assets` se resuelve desde `firmware/`, y cada `idf.py build` regenera
`build/assets.bin` aunque no lo grabe.

Como referencia visual, [`docs/esp32.gif`](docs/esp32.gif) es el ejemplo
animado elegido. Hoy solo vive en la documentacion: no se empaqueta ni se
reproduce en el ESP32. **No cumple todavia la regla de tamano**: mide 800x600.
Para usarlo como `splash.gif` hay que exportarlo a 240 px de ancho; su
contenido (552x538) queda en 240x234.

## Recuperacion

```
powershell -File scripts/restore_factory.ps1 -Port COM3
```

Restaura los 16 MB completos desde el backup de fabrica.

Mientras la placa siga con la tabla de particiones original, hay un atajo que
vuelve al demo sin reflashear nada:

```
python -m esptool --port COM3 erase_region 0xF000 0x2000
```

Eso deja `otadata` en blanco y el bootloader cae a `factory`. **Deja de servir
despues del primer flash con la tabla de este repo**, porque los offsets ya no
coinciden.

## Si la placa no responde por USB

El firmware de fabrica aisla los GPIO en light sleep y el USB-Serial-JTAG deja
de contestar; Windows conserva el COM enumerado pero muerto (`ERROR_GEN_FAILURE`
al abrir, `ERROR_SEM_TIMEOUT` al escribir).

Manten apretado **BOOT (GPIO0)**, enchufa el USB, espera 2 s y solta: entra al
ROM antes de que corra el firmware.

Este repo apaga `CONFIG_PM_ENABLE`, asi que no hay light sleep automatico y el
USB-Serial-JTAG nunca se duerme.

La linea `sleep_gpio: Configure to isolate all GPIO pins in sleep state`
**igual aparece** en el boot log de este firmware. La emite
`CONFIG_PM_SLP_DISABLE_GPIO`, que sigue habilitada y solo deja preparada la
configuracion por si hubiera un sleep; sin gestion de energia no se dispara.
Ver esa linea no significa que el COM se vaya a colgar.

## Particiones

Definidas en [`partitions/default.csv`](partitions/default.csv).

`model` (952K) queda **reservada y vacia** desde el dia 1. No se usa todavia,
pero cambiar la tabla mas adelante invalida OTA y borra la NVS del campo, asi
que el espacio se reserva ahora.

`assets` (7M) es la particion SPIFFS de este proyecto. Hoy **no tiene
contenido propio**: el firmware no la monta ni el build la genera. Queda
reservada para la imagen de inicio (fase 1a) y, mas adelante, iconos y sonidos.
En una placa que nunca recibio `assets-flash`, esa zona (`0x900000`-`0xFFFFFF`)
conserva restos de las particiones de fabrica `ota_0` y `storage`, que la
pisaban; por eso el firmware no puede asumir que monta ni que lo que lee es
valido. El BSP monta SPIFFS por
etiqueta y su default es `storage`, por eso `sdkconfig.defaults` ya fija
`CONFIG_BSP_SPIFFS_PARTITION_LABEL="assets"`. Cuando se monte, el punto de
montaje va a ser el default del BSP, `/spiffs`.

`nvs` guarda configuracion del usuario (fase 4 en adelante: redes WiFi). Nunca
se versiona ni se vuelca al repo.

## Contribuir

Issues y PRs son bienvenidos. Antes de abrir un PR:

- `idf.py build` pasa desde un clone limpio con ESP-IDF 5.5.x oficial.
- Una rama y un PR por cambio: `feat/...`, `fix/...`, `docs/...`.
- El arranque no depende de WiFi, de `assets` ni de la microSD. Si faltan, se
  ve el splash de siempre.
- Nada de secretos, credenciales WiFi, volcados de flash o NVS, ni binarios de
  terceros.
- El PMU sigue en solo lectura. Escribir rieles o configurar la carga va en un
  PR propio que justifique cada registro.
- Un dato de hardware nuevo va a [`docs/hardware.md`](docs/hardware.md), con su
  fuente.
- Una imagen o sonido nuevo va con su autor y su licencia en [NOTICE](NOTICE),
  y la licencia tiene que permitir redistribuirlo y modificarlo.
- Las credenciales WiFi viven en NVS (`wifi`); no se commitean ni se loguean.
  El SoftAP de provision es abierto y de corta vida: se apaga al conectar o al
  cerrar la pantalla.

Las reglas de codigo (servicios, apps, hilos y LVGL) estan en
[`docs/arquitectura.md`](docs/arquitectura.md).

## Licencia

Codigo propio bajo [MIT](../LICENSE). Atribuciones de terceros y de los
recursos graficos (autor y licencia de cada imagen) en [NOTICE](NOTICE).

Este repositorio no redistribuye binarios de Waveshare ni de Xiaozhi, ni
volcados de flash o NVS de la placa.

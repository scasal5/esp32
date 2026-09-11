# ws183-os

Firmware propio para la **Waveshare ESP32-S3-Touch-LCD-1.83**, construido sobre
ESP-IDF oficial y el BSP publicado por Waveshare — no un fork del firmware de
fabrica.

Estado: **v0**, arranca el panel y dibuja un splash. Nada mas todavia.

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

## Compilar

Requiere **ESP-IDF 5.5.x oficial**. No uses el arbol del vendedor: el firmware
de fabrica se compilo con un IDF marcado `-dirty`, que no es reproducible.

```
idf.py set-target esp32s3
idf.py build
```

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
`panel up: 240x284`, y el splash en pantalla. Si el log vive pero la pantalla
queda negra, el problema es backlight o PMU, no el USB.

El firmware **se queda en el splash**: `app_main` retorna despues de dibujarlo
y la task de LVGL mantiene la pantalla. No es un cuelgue, es el estado final
de v0.

El warning `ledc: GPIO 40 is not usable, maybe conflict with others` es
cosmetico: el backlight enciende igual.

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

`assets` es la particion SPIFFS de este proyecto. El BSP monta SPIFFS por
etiqueta y su default es `storage`, por eso `sdkconfig.defaults` fija
`CONFIG_BSP_SPIFFS_PARTITION_LABEL="assets"`.

## Licencia

Codigo propio bajo [MIT](LICENSE). Atribuciones de terceros en [NOTICE](NOTICE).

Este repositorio no redistribuye binarios de Waveshare ni de Xiaozhi, ni
volcados de flash o NVS de la placa.

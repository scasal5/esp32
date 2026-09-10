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
| Pantalla | 240 x 284, controlador ST7789 por SPI |
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

El Component Manager baja el BSP y sus dependencias en el primer build.

## Flashear

> El primer `idf.py flash` **sobrescribe bootloader, tabla de particiones y
> `factory`**. Hace falta tener el backup de fabrica antes de correrlo.

```
idf.py -p COM3 flash monitor
```

Esperado en el monitor: log por USB-Serial-JTAG, `panel up: 240x284`, y el
splash en pantalla. Si el log vive pero la pantalla queda negra, el problema
es backlight o PMU, no el USB.

## Recuperacion

```
powershell -File scripts/restore_factory.ps1 -Port COM3
```

Restaura los 16 MB completos desde el backup de fabrica.

Mientras la placa siga con la tabla de particiones original, hay un atajo que
vuelve al demo sin reflashear nada:

```
esptool --port COM3 erase-region 0xF000 0x2000
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

Este repo desactiva el light sleep en `sdkconfig.defaults` justamente para no
reproducir ese comportamiento.

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

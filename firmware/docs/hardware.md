# Hardware verificado

Relevamiento hecho sobre una unidad real el 10/09/2026, leyendo el chip por
USB-Serial-JTAG con esptool 5.4.0. Todo solo lectura.

Cada dato indica su procedencia. Nada viene de una ficha tecnica.

## Silicio

| Dato | Valor | Fuente |
|---|---|---|
| SoC | ESP32-S3 (`chip_id 9`, magic `0x00000009`) | ROM |
| Encapsulado | QFN56 (`PKG_VERSION 0`) | eFuse |
| Revision | v0.2 | eFuse |
| CPU | Xtensa LX7 dual-core + LP core, 240 MHz | boot log |
| Cristal | 40 MHz | ROM |
| PSRAM | 8 MB octal, AP Memory, 3V3, 80 MHz | eFuse + boot log |
| Flash | 16 MB externa, JEDEC mfr `0x20`, dev `0x4018` | ROM |
| MAC base | `44:1B:F6:84:DA:88` | eFuse |
| ID unico | `1d26f819 976246cb 873590f0 de3f29ff` | eFuse |
| Calibracion | `TEMP_CALIB` -10,1 C, ADC V1 | eFuse |
| VDD_SPI | 3,3 V forzado por eFuse | eFuse |

## Perifericos

| Funcion | Componente | Evidencia | Fuente |
|---|---|---|---|
| PMU / bateria | AXP2101 | simbolo `14XPowersAXP2101E` | binario |
| Panel | ST7789, 240x284 | `BSP_LCD_H_RES` / `BSP_LCD_V_RES` | fuente del BSP |
| Tactil | CST816S | `esp_lcd_touch_new_i2c_cst816s` | boot log |
| IMU | QMI8658 | `QMI8658_ADDRESS_HIGH` | boot log |
| Codec audio | ES8311 | `Open codec device OK` | boot log |
| ADC microfonos | ES7210, MIC1 + MIC2 | boot log | boot log |
| Backlight | LEDC PWM | `BSP_DISPLAY_BRIGHTNESS_LEDC_CH` | Kconfig del BSP |
| microSD | ranura SDMMC | `sdmmc_init_ocr 0x107` sin tarjeta | boot log |
| RTC | presente en la placa, sin usar por el firmware de fabrica | descripcion del BSP | registro |

El driver del ES7210 soporta cuatro microfonos; esta placa habilita dos.

## Seguridad: chip completamente abierto

Ningun eFuse de bloqueo esta quemado.

```
Secure Boot            DESHABILITADO
Flash Encryption       DESHABILITADO
SPI_BOOT_CRYPT_CNT     0b000
WR_DIS / RD_DIS        0x00000000 / 0b0000000
BLOCK_KEY0..5          USER / EMPTY
Modo download          HABILITADO
JTAG                   HABILITADO
SECURE_VERSION         0
```

## Tabla de particiones de fabrica

Distinta de la de este repo. Se documenta para poder restaurar.

| Particion | Tipo | Offset | Tamano | Contenido |
|---|---|---|---|---|
| bootloader | — | `0x000000` | ~32 KB | ESP-IDF v5.5.1 |
| tabla | — | `0x008000` | 4 KB | 7 entradas |
| nvs | data/nvs | `0x009000` | 24 KB | |
| otadata | data/ota | `0x00F000` | 8 KB | borrada (0xFF) |
| phy_init | data/phy | `0x011000` | 4 KB | en blanco |
| model | data/spiffs | `0x012000` | 952 KB | WakeNet9 |
| factory | app | `0x100000` | 4736 KB | demo Waveshare |
| ota_0 | app | `0x5A0000` | 6144 KB | Xiaozhi 1.8.5 |
| storage | data/spiffs | `0xBA0000` | 4436 KB | imagenes y audio |

MD5 de la tabla: `48169c71128b18621e2ca570abc41daa`.
Termina en `0xFF5000`; quedan 44 KB sin asignar.

`otadata` en blanco hace que el bootloader arranque `factory`, no `ota_0`. Por
eso la placa de fabrica corre el demo de Waveshare y no Xiaozhi.

## El COM fantasma

El firmware de fabrica activa
`sleep_gpio: Configure to isolate all GPIO pins in sleep state`. En light sleep
el USB-Serial-JTAG deja de atender transferencias, pero Windows conserva el nodo
enumerado con los descriptores viejos:

```
CreateFile  \\.\COM3      OK
GetCommState              OK - 8N1, 115200
SetCommState              ERROR_GEN_FAILURE (31)
WriteFile                 ERROR_SEM_TIMEOUT (121)
escucha pasiva            0 bytes
```

Salida: BOOT (GPIO0) apretado, enchufar USB, esperar 2 s, soltar.

## Pendiente de verificar

- Mapa de pines GPIO por periferico
- Direcciones I2C del bus
- Capacidad y quimica de la bateria
- Modelo exacto del RTC

## Reproducir el relevamiento

```
esptool  --port COM3 chip-id
esptool  --port COM3 flash-id
esptool  --port COM3 get-security-info
espefuse --port COM3 summary
esptool  --port COM3 read-flash 0x0 0x10000 first64k.bin
```

El descriptor `esp_app_desc_t` de cada app vive en `offset + 0x20`.

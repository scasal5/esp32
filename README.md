# ESP32-S3 Touch LCD 1.83 — firmware propio

Objetivo: construir un sistema **realmente open source** para esta placa (Waveshare ESP32-S3-Touch-LCD-1.83), no solo reflashear binarios de terceros.

Open source, en este contexto, significa:
- código compilable por terceros,
- BSP abierto,
- splash propio,
- flasheo reproducible.

## Camino recomendado

| Camino | Qué es | Resultado |
|---|---|---|
| A. Flashear Xiaozhi/Brookesia | Producto de otro | Rápido, poco propio |
| **B. ESP-IDF + BSP Waveshare + app propia** | **Sistema propio** | **Recomendado** |
| C. Desde cero sin BSP | Rehacer drivers | Muy lento, poco rentable |

## 0) Red de seguridad (una vez)

Hacer dump de particiones actuales antes de tocar nada (fuera de este repo). No subir NVS/Wi-Fi ni binarios de terceros al Git.

Particiones observadas en fábrica:
- bootloader `0x0` 32 KB
- partition table `0x8000` 4 KB
- otadata `0xF000` 8 KB
- factory `0x100000` 4736 KB
- ota_0 `0x5A0000` 6144 KB
- model `0x12000` 952 KB
- storage `0xBA0000` resto

## 1) Toolchain limpio

- Python 3.13
- ESP-IDF oficial 5.5.x (no vendor "dirty")
- esptool 5.4 en venv
- target `esp32s3`, flash 16 MB (DIO/QIO 80 MHz), PSRAM octal OPI

Primero: compilar `idf.py hello_world`.

## 2) Primer encendido útil en este hardware

Usar como referencia (no como repo final):
- `waveshareteam/ESP32-S3-Touch-LCD-1.83`
- ejemplos `01_AXP2101` y `02_lvgl_demo_v9`

Meta: validar AXP2101, LCD ST7789P 240x284, touch CST816 y backlight.

## 3) Estructura de repo propio

```text
firmware/
├── README.md
├── LICENSE
├── NOTICE
├── docs/{hardware.md,flashing.md}
├── partitions/default.csv
├── sdkconfig.defaults
├── main/{app_main.c,boot_splash.*,ui/,services/}
├── components/
├── idf_component.yml
└── scripts/{flash.sh,dump_factory.sh}
```

Dependencias sugeridas (`idf_component.yml`):
- `waveshare/esp32_s3_touch_lcd_1_83`
- `lvgl/lvgl` (v9)
- `espressif/esp_codec_dev`

## 4) Particiones propias (16 MB)

Base sugerida:
- `nvs` 24K
- `otadata` 8K
- `phy_init` 4K
- `factory` 4M
- `ota_0` 4M
- `assets` ~7M

`model` solo cuando se use ESP-SR/WakeNet.

## 5) Splash de arranque (app, no bootloader)

Orden recomendado para evitar pantalla negra larga:
1. I2C + AXP2101
2. Panel ST7789P + backlight
3. Pintar splash
4. Luego Wi-Fi/LVGL/audio/IMU

Evitar aislar USB-Serial-JTAG al inicio/sleep para no perder el COM en Windows.

## 6) Flasheo de desarrollo

Durante prototipo, escribir solo bootloader + tabla + app en `factory`:

```bash
idf.py -p <PORT> flash monitor
```

Conservar `ota_0` como plan B hasta estabilizar `factory`. Evitar `erase_flash` sin dump completo.

## 7) Definición de “open source real” para este proyecto

Este repo debe incluir:
- código compilable en ESP-IDF 5.5,
- `sdkconfig.defaults` + particiones,
- guía de flash y recovery,
- CI de `idf.py build`,
- cero secretos/NVS/binarios de terceros.

## 8) Decisión de producto

1. UI/companion propio → empezar por Camino B.
2. Asistente de voz primero → partir de Xiaozhi y adaptar board/splash.
3. Ambos → base B + servicio de voz separado.

Estado recomendado actual: **Camino B**.

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
- esptool: ESP-IDF 5.5 trae la **4.12**. Los subcomandos con guion
  (`write-flash`, `chip-id`) solo existen en esptool 5.x; usar siempre la forma
  con guion bajo (`write_flash`, `chip_id`), valida en las dos
- target `esp32s3`, flash 16 MB (DIO/QIO 80 MHz), PSRAM octal OPI

En Windows, el alias de Python de Microsoft Store se antepone al Python real y
hace fallar `install.bat` con error 49. Conviene desactivarlo en *Configuracion
→ Aplicaciones → Alias de ejecucion de aplicaciones*.

Primero: compilar `idf.py hello_world`.

## 2) Primer encendido útil en este hardware

Usar como referencia (no como repo final):
- `waveshareteam/ESP32-S3-Touch-LCD-1.83`
- ejemplos `01_AXP2101` y `02_lvgl_demo_v9`

Meta: validar AXP2101, LCD 240x284, touch CST816 y backlight.

El panel es un **ST7789** (`esp_lcd_panel_st7789`). "ST7789P" es el nombre
comercial de Waveshare; no aparece como simbolo en el SDK ni en el binario.

## 3) Estructura de repo propio

```text
firmware/
├── CMakeLists.txt
├── NOTICE
├── README.md
├── docs/{hardware.md,flashing.md}
├── partitions/default.csv
├── sdkconfig.defaults
├── main/{app_main.c,boot_splash.*,idf_component.yml,ui/,services/}
├── components/
└── scripts/restore_factory.ps1
```

`LICENSE` vive en la raiz del repositorio, no dentro de `firmware/`.

El manifiesto `idf_component.yml` va **dentro de `main/`**: pertenece al
componente, no al proyecto. Puesto al lado de `main/`, el Component Manager lo
ignora en silencio y el build falla despues con headers que no aparecen.

En Windows los scripts son `.ps1`. El flasheo de desarrollo es
`idf.py -p COM3 flash monitor`, y el dump de fabrica ya esta hecho, asi que
`scripts/` solo necesita la recuperacion.

Dependencias (`main/idf_component.yml`):
- `waveshare/esp32_s3_touch_lcd_1_83` `^2.0.0`
- `lvgl/lvgl` `^9` — pin obligatorio: el BSP declara `>=8,<10` y con LVGL 8 no
  existen `lv_display_t` ni `lv_screen_active()`
- el resto (`esp_lvgl_port`, `esp_codec_dev`, `esp_lcd_touch_cst816s`,
  `esp_lcd_panel_io_additions`) entra por transitividad del BSP

## 4) Particiones propias (16 MB)

Base sugerida:
- `nvs` 24K
- `otadata` 8K
- `phy_init` 4K
- `factory` 4M
- `ota_0` 4M
- `assets` ~7M

`model` (952K) se reserva **vacia desde el dia 1**, aunque todavia no se use.
Cambiar la tabla de particiones mas adelante invalida OTA y borra la NVS de los
equipos en campo: el espacio se aparta ahora o la migracion es destructiva.

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

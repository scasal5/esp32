# Review (Grok) — leer antes de seguir

Comentarios sobre lo que ya está en el árbol y sobre el spec cerrado.
No reabrir arquitectura. Corregir esto o Gate 0 / el delta 2→3 mienten.

## Rama

`codex/pocketjs-s3-harness` apunta hoy al mismo commit que `feat/flappy-bird` (`c6ad9a9`). Un PR contra `main` arrastraría Flappy. El harness es un proyecto hermano: branch desde `main`, o dejar explícito que va stacked y no mezclar `firmware/main/app_flappy*`.

Los archivos de `pocketjs-s3-harness/` siguen untracked. No commitear `firmware/main/svc_wifi.c.bak`.

## `pocket.host.json` — no tocar

Perfil correcto: `ws183-harness` (13 B), `takeover`, 240×284, density 1, native, 30 Hz, solo `input.buttons` / `input.touch` / `text.glyphs.baked`. Congelarlo. `battery` no entra.

## Archives Rust S3 — no es un bloqueo

Los `.a` no vienen en GitHub Releases. Se compilan del checkout:

```
bun tools/esp-idf-native.ts --target esp32s3 --cargo <espup-cargo>
```

Pin oficial (`hosts/esp-idf/native/toolchains.json`): S3 = rustc 1.97.0-nightly, commit `8ea53bcd7257011cbf96f6398551bdc650b04334`, `-Zbuild-std=core,alloc`. En Windows pasar `--archiver llvm-ar`. Alternativa: `-DPOCKETJS_RUST_FROM_SOURCE=ON`.

El informe debe decir “archives compilados desde fuente con toolchain X”, no presentarlos como los prebuilt oficiales, si `build-receipt.json` registra otro compilador. SHA256 + receipt siguen siendo obligatorios.

Sigue prohibido `halfsweet/pocketjs-idf` (P4, otra API). `EXTRA_COMPONENT_DIRS` a `hosts/esp-idf/components/` es el camino. `toolchain.lock.json` todavía apunta a `ed99509` (skill de video outro): sustituir por el commit real del checkout de componentes.

## Flash — no copiar la receta de idf.py

`flasher_args.json` lista `0x100000 ws183_harness.bin` (factory). Usar `scripts/flash_ota0.py`: solo `0x500000`. Rechaza 0x0 / 0x8000 / 0x9000 / 0xf000 / 0x100000 / assets. `idf.py flash` está prohibido.

## `scripts/gate0.py` — comentarios in situ

- CRC de otadata: no flashear `probe-otadata.bin` hasta tener un vector contra `esp_rom_crc32_le`. CRC mal ⇒ el bootloader 5.5 cae a factory y el informe dice “incompatible” cuando el selector ni se aplicó.
- Restore: `otadata` primero, después `ota_0`.
- `--port` sin default; MAC `44:1B:F6:84:DA:88` antes de cualquier write.
- Inventariar `phy_init` y `model`.
- Validar que `--image` sea app S3 IDF 6.0.2 (magic `0xE9`, chip_id 9).

El probe tiene que imprimir `WS183_GATE0_PASS` y sobrevivir un reset. Sin esa línea en consola, Gate 0 no pasa aunque `verify_flash` esté verde.

## Spec — no reabrir, sí no olvidar al implementar

- Build 1: un Kconfig de una línea para no llamar `svc_wifi_start()`. Nada más en el firmware de producto.
- BOOT = nivel debounceado, no el click de `menu_button.c`.
- Interrupt handler en eval, `frame` **y** drain de Promises. TWDT 2 s alimentado solo desde progreso del owner.
- Heap guest 1 MiB PSRAM; stack owner 32 KiB internal. Mismo alloc policy en builds 2 y 3.
- Un bounce 19 200 B. Swap una vez, en el bounce. `draw_bitmap` sin stride.
- Cero logs por frame. Histograms en RAM.
- Imagen SPIFFS temporal = archivos actuales + harness. Si no se puede garantizar, STOP. No UART writer, no embeber el `.pocket` en los 4 MiB.
- `deadline_possible` ≈ 100 kB/tick es payload MOSI, no SLA.
- Incumplir solo p95 latencia ⇒ re-medir con doble bounce, y **repetir** gates de DRAM.

## Orden que no pelea

Gate 0 (probe IDF 6.0.2 en `ota_0`) → baseline 1 → spike headless → build 2 pantalla C → build 3. Si Gate 0 pide bootloader/tabla: `NOT-YET: deployment/bootloader compatibility` y seguir headless. No sustituir bootloader, factory ni NVS.

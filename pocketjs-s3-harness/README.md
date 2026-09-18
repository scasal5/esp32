# ws183 PocketJS cost harness

This sibling ESP-IDF project keeps hardware, scheduling, storage and Wi-Fi in C/C++.
It does not link LVGL, the full BSP or `pocketjs_runner`. It lives on the stacked
`codex/pocketjs-s3-harness` branch; the product apps are not migrated.

Build 1 is the existing IDF 5.5.1 product with optional telemetry. Build 2 is the
native IDF 6.0.2 host. Build 3 adds PocketJS to the same host. Only **3 minus 2**
attributes cost to the runtime. Compare identical profiles, Wi-Fi, SPIRAM policy
and bounce count. `scripts/report.py` refuses a PASS without physical evidence.

## Dependencies and builds

Pins are in `toolchain.lock.json`; component resolution is in `dependencies.lock`.
Provision IDF 6.0.2 in `.deps/esp-idf`, its tools in `.tools`, PocketJS at the pinned
commit in `.deps/pocketjs`, and Bun 1.3.14 at `.tools/bun/bun-windows-x64/bun.exe`.
Run IDF's `install.ps1 esp32s3` with `IDF_TOOLS_PATH` pointing to `.tools`, and run
`bun install --frozen-lockfile` in the PocketJS checkout. These directories are ignored.

Native archives are **locally built from the pinned source**, not downloaded
PocketJS release binaries. `scripts/native_archives.ps1` invokes the upstream build
and verifies its receipts. It requires esp-rs rustc 1.97.0-nightly, commit
`8ea53bcd7257011cbf96f6398551bdc650b04334`, Rust sources, and a Windows MSVC linker,
CRT and SDK libraries. The current isolated layout is `.tools/rust-dist/esp`,
`.tools/msvc` and `.tools/sdk`. Target: `xtensa-esp32s3-none-elf` with
`-Zbuild-std=core,alloc`. The script compensates for upstream's POSIX PATH separator
on Windows; it does not change native sources or receipt verification.

```powershell
./scripts/package.ps1
./scripts/build.ps1 -Mode probe
./scripts/build.ps1 -Mode headless
./scripts/build.ps1 -Mode native -Profile perf
./scripts/build.ps1 -Mode pocket -Profile perf
./scripts/build.ps1 -Mode native -Profile perf -Variant sta -ExtraDefaults config/sdkconfig.sta
./scripts/build.ps1 -Mode pocket -Profile perf -Variant sta -ExtraDefaults config/sdkconfig.sta
./scripts/build.ps1 -Mode pocket -Variant golden -ExtraDefaults config/sdkconfig.golden
./scripts/baseline.ps1 -Wifi off
./scripts/baseline.ps1 -Wifi sta
```

Each build has its own sdkconfig and receipt. Use a new `-Variant` when changing
defaults: IDF preserves values in an existing sdkconfig. The PERF profile uses
`-O2` with assertions. Golden is a correctness configuration, not PERF.

The local `pocketjs_guest` adapter compiles upstream guest sources with a reviewed
QuickJS 0.14.0 immutable-buffer patch. `prepare_quickjs.py` only accepts the exact
published source SHA256 and writes a build copy. It rejects unknown source; the
headless suite checks immutable `reverse`, species destination and read-only view
behavior on the actual runtime.

## Deployment and recovery

**Never use `idf.py flash`**: its generated recipe writes factory, bootloader and
partition table. Use `flash_ota0.py`, which validates an S3 image within 4 MiB and
checks the board MAC. Serial ports must be supplied explicitly for writes.

```powershell
python scripts/gate0.py backup --port COM3 --backup D:/private/ws183-backup
python scripts/gate0.py probe --port COM3 --backup D:/private/ws183-backup --image build-probe-debug/ws183_harness.bin
python scripts/gate0.py restore --port COM3 --backup D:/private/ws183-backup
```

Keep the complete 16 MiB recovery image outside Git. Gate 0 requires the installed
bootloader to boot the probe from ota_0, a reset, and restored product boot. The
probe prints `WS183_GATE0_PASS` and the ROM otadata CRC vector. If bootloader or
partition replacement is required, stop physical deployment with
`NOT-YET: deployment/bootloader compatibility`.

`assets.py prepare` uses mkspiffs 0.2.3, 4096-byte blocks, 256-byte pages and the
existing 7 MiB partition. It extracts the recovery image, adds `harness.pocket`,
re-extracts the new image and compares every filename and SHA256. It refuses an
existing `harness.pocket`. `install` first verifies the image currently on flash.
For a subsequent harness revision, `--previous-proof` permits replacing only a
previously verified image with the same original files. No UART file protocol is
used. `restore` restores and verifies the exact original image.

```powershell
python scripts/assets.py prepare --backup D:/private/ws183-backup --mkspiffs PATH_TO_MKSPIFFS --package out/app/harness.pocket
python scripts/assets.py install --port COM3 --backup D:/private/ws183-backup --mkspiffs PATH_TO_MKSPIFFS
python scripts/flash_ota0.py --port COM3 --build build-pocket-perf
# At the end, restore assets, then original boot selection and ota_0:
python scripts/assets.py restore --port COM3 --backup D:/private/ws183-backup --mkspiffs PATH_TO_MKSPIFFS
python scripts/gate0.py restore --port COM3 --backup D:/private/ws183-backup
```

Do not publish recovery images: they may contain credentials. Verify the original
console and protected regions before recording restoration PASS.

## Runtime contracts

`pocket.host.json` is the canonical admission profile; a generated C contract
pins its hash. `battery` is a private QuickJS extension with `apiVersion=1`, outside
the profile and capability list. It is installed before eval. PMU sampling occurs
every three seconds on another task under the shared I2C mutex; JS reads an
immutable per-turn snapshot without I/O. Wi-Fi uses RAM-only configuration and
reads saved credentials without printing them or migrating them.

The initial budget is a 1 MiB guest heap preferring PSRAM, a 32 KiB internal owner
stack and a 24 KiB JS stack limit. The owner is watched by a two-second panic TWDT.
A separate timer invokes the guest's supported interrupt mechanism at 500 ms for
eval and 20 ms for turn/Promise drain. The owner alone feeds the watchdog during
normal progress. A failed turn never reaches the presenter.

The timer uses absolute deadlines `t0 + floor(n*1000000/30)` and only notifies the
owner. Late slots are counted and skipped, not replayed as an unlimited burst.
Metrics use fixed RAM histograms; no per-frame logging. Samples are emitted every
ten seconds. Full-frame transfer alone takes 45.44 ms at 24 MHz.

The presenter renders full-width strips into a native 240x284 PSRAM framebuffer,
packs dirty windows into one 19,200-byte internal DMA bounce, swaps bytes exactly
once and waits on completion. It commits only after every transfer succeeds;
failure aborts and invalidates the renderer target. A second bounce is an explicit
follow-up variant, with memory gates repeated.

`app/main.ts` implements the six scenarios using the official typed HostOps.
`app/main.tsx` preserves the initial Solid experiment, which exceeded the initial
startup budgets on this board. The measured package does not include Solid.
The compiler still uses the standard manifest/profile, baked glyphs and package
format. Minification uses Bun and upstream's package codec, with its own receipt.

## Physical validation

Console commands: `scenario 0..5`, `smoke`, `soak`, `report`, `fail-transfer`,
`wifi <ssid> <password>`. Credentials are not echoed or persisted. Headless accepts
`smoke` to rerun the suite and `report` to return its last result.

`smoke` runs 10,000 ticks; `soak` runs at least two hours and induces a reconnect
every ten minutes. The scenario cycle gives static 70 seconds and each remaining
scenario 50 seconds. Run smoke with Wi-Fi never initialized, then connected STA.
Soak requires connected STA. Physical touch is required for IRQ latency evidence.

```powershell
python scripts/capture.py --port COM3 --command smoke --seconds 700 --out out/smoke.log
python scripts/capture.py --port COM3 --command soak --seconds 7220 --out out/soak.log
python -m unittest discover -s tests
python scripts/report.py --log out/smoke.log --log out/soak.log --receipt build-pocket-perf-sta/receipt.json --baseline-receipt build-native-perf-sta/receipt.json --out out/report.json
```

Golden compares native full-reference and incremental framebuffers by CRC32 and
memcmp before swapping, including narrow edges, corners, 40-line strips and
overlap cases. Exercise `fail-transfer` and require a successful recovery. Golden
memory and timing are excluded from PERF results.

Absolute gates: app <=4 MiB, internal low-water >=48 KiB, largest internal block
>=16 KiB and free PSRAM >=1 MiB. Review equivalent-cycle memory slopes, resets,
corruption, reconnects and restore evidence. Partial IRQ-to-present p95 aims for
<=50 ms; a latency-only miss requires a double-bounce benchmark before rejection.
`deadline_possible` classifies payload <=100,000 bytes, not a timing guarantee.
Missing evidence remains NOT-YET; successful native or headless tests cannot
substitute for graphical correctness, performance or the physical soak.

"""Flash the harness app to ota_0 only.

idf.py flash writes bootloader @ 0x0, table @ 0x8000, otadata @ 0xf000 and
the app @ 0x100000 (factory). That recipe is forbidden on this board.

This wrapper takes the app image from flasher_args.json and writes it to
0x500000. It never writes 0x0, 0x8000, 0x9000, 0xf000, 0x100000 or assets.
Selecting ota_0 remains gate0.py probe; restoring remains gate0.py restore.
"""
from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

OTA0 = 0x500000
OTA0_SIZE = 0x400000
ESP32S3_CHIP_ID = 9
FORBIDDEN = {
    0x0: "bootloader",
    0x8000: "partition table",
    0x9000: "nvs",
    0xF000: "otadata",
    0x100000: "factory",
    0x900000: "assets",
}


def run_esptool(port: str, *args: str) -> None:
    subprocess.run(
        [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port,
         "--baud", "460800", *args],
        check=True,
    )


def load_app(build: Path) -> Path:
    args_path = build / "flasher_args.json"
    if not args_path.is_file():
        raise SystemExit(f"missing {args_path}; build first")
    spec = json.loads(args_path.read_text(encoding="utf-8"))
    for offset_str in spec.get("flash_files", {}):
        offset = int(offset_str, 16)
        if offset in FORBIDDEN and offset != 0x100000:
            print(f"note: idf.py would write {FORBIDDEN[offset]} at {offset_str}; ignored",
                  file=sys.stderr)
    app_ent = spec.get("app") or {}
    rel = app_ent.get("file")
    if not rel:
        raise SystemExit("no app image in flasher_args.json")
    listed = int(str(app_ent.get("offset", "0")), 16)
    if listed != 0x100000:
        raise SystemExit(f"app offset in flasher_args.json is 0x{listed:x}, not factory; refusing")
    return (build / rel).resolve()


def check_image(path: Path) -> int:
    data = path.read_bytes()
    size = len(data)
    if size == 0 or size > OTA0_SIZE:
        raise SystemExit(f"app image size {size} is outside 1..{OTA0_SIZE}")
    if data[0] != 0xE9:
        raise SystemExit("app image magic is not 0xE9")
    chip_id = struct.unpack_from("<H", data, 12)[0]
    if chip_id != ESP32S3_CHIP_ID:
        raise SystemExit(f"chip_id {chip_id} is not ESP32-S3 ({ESP32S3_CHIP_ID})")
    return size


def read_mac(port: str) -> str:
    result = subprocess.run(
        [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port, "read_mac"],
        check=True, capture_output=True, text=True,
    )
    text = result.stdout + result.stderr
    for line in text.splitlines():
        if "MAC:" in line.upper() or "mac" in line.lower():
            parts = line.replace(":", " ").split()
            hex_bytes = [p for p in parts if len(p) == 2 and all(c in "0123456789abcdefABCDEF" for c in p)]
            if len(hex_bytes) >= 6:
                return ":".join(b.upper() for b in hex_bytes[:6])
    raise SystemExit("could not parse MAC from esptool read_mac")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True, help="IDF build directory")
    parser.add_argument("--port", required=True, help="serial port; no default")
    parser.add_argument("--expect-mac", help="abort if the chip MAC does not match")
    args = parser.parse_args()

    app = load_app(args.build)
    size = check_image(app)
    if args.expect_mac:
        mac = read_mac(args.port)
        expect = args.expect_mac.replace("-", ":").upper()
        if mac != expect:
            raise SystemExit(f"MAC {mac} != {expect}; refusing to flash")

    print(f"Writing {app} ({size} bytes) to ota_0 @ 0x{OTA0:x}")
    print("Refusing bootloader, table, otadata, factory, NVS and assets.")
    run_esptool(args.port, "write_flash", hex(OTA0), str(app))
    run_esptool(args.port, "verify_flash", hex(OTA0), str(app))
    print("ota_0 updated. factory/bootloader/NVS untouched.")
    print("If the board still runs factory, select ota_0 with: python scripts/gate0.py probe ...")


if __name__ == "__main__":
    main()

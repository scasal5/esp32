"""Read-only inventory/backup by default. Never writes a bootloader or table.

Run with the IDF Python environment. Private flash copies MUST live outside git.
The probe/restore actions operate only on ota_0 and otadata, after inventory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[2]
# REVIEW: default.csv also has phy_init @ 0x11000 (4K) and model @ 0x12000 (952K).
# They must appear in the receipt even if we never rewrite them. Omitting them
# hides a table mismatch and makes a later full-flash restore incomplete.
EXPECTED = {"nvs": (0x9000, 0x6000), "otadata": (0xF000, 0x2000),
            "factory": (0x100000, 0x400000), "ota_0": (0x500000, 0x400000),
            "assets": (0x900000, 0x700000)}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def partitions(data):
    result = {}
    for pos in range(0, 0xC00, 32):
        entry = data[pos:pos + 32]
        if entry[:2] != b"\xaa\x50":
            break
        _, kind, subtype, offset, size, name, flags = struct.unpack("<HBBII16sI", entry)
        result[name.rstrip(b"\0").decode()] = dict(type=kind, subtype=subtype,
                                                 offset=offset, size=size, flags=flags)
    return result


def ota_entries(data):
    entries = []
    for offset in (0, 4096):
        seq, label, state, crc = struct.unpack_from("<I20sII", data, offset)
        # REVIEW: zlib.crc32(data, 0xFFFFFFFF) is NOT proven equal to
        # esp_rom_crc32_le(UINT32_MAX, &ota_seq, 4). Wrong CRC => bootloader
        # treats otadata as empty and falls back to factory. Gate 0 then looks
        # like "bootloader incompatibility" when the selector never took.
        # Add a unit vector (seq=1) against IDF or a known-good otadata dump
        # before any write_flash of probe-otadata.bin.
        valid = seq not in (0, 0xFFFFFFFF) and crc == zlib.crc32(struct.pack("<I", seq), 0xFFFFFFFF)
        entries.append(dict(sequence=seq, state=state, valid=valid))
    return entries


def run(port, *args):
    subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port,
                    "--baud", "460800", *map(str, args)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["backup", "probe", "restore"])
    # REVIEW: do not default COM3. Identify by MAC (board is 44:1B:F6:84:DA:88
    # in firmware/docs/hardware.md) via `esptool read-mac`, then bind the port.
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--backup", type=Path, required=True)
    parser.add_argument("--image", type=Path)
    args = parser.parse_args()
    folder = args.backup.resolve()
    if folder.is_relative_to(ROOT):
        parser.error("Private backups must be outside the repository")
    folder.mkdir(parents=True, exist_ok=True)
    full = folder / "flash.bin"
    receipt = folder / "gate0.json"
    if args.action == "backup":
        if full.exists() or receipt.exists():
            parser.error("Use a new backup directory; never overwrite the recovery copy")
        run(args.port, "flash_id")
        run(args.port, "read_flash", "0", "0x1000000", full)
        data = full.read_bytes()
        if len(data) != 0x1000000:
            raise RuntimeError("Incomplete flash backup")
        table = partitions(data[0x8000:0x9000])
        for name, (offset, size) in EXPECTED.items():
            if name not in table or (table[name]["offset"], table[name]["size"]) != (offset, size):
                raise RuntimeError("NOT-YET: unexpected partition layout; backup retained")
        regions = {}
        for name, (offset, size) in EXPECTED.items():
            region = data[offset:offset + size]
            (folder / (name + ".bin")).write_bytes(region)
            regions[name] = sha(region)
        receipt.write_text(json.dumps(dict(flash_sha256=sha(data), partitions=table,
            region_sha256=regions, otadata=ota_entries(data[0xF000:0x11000]),
            status="BACKED_UP_NOT_BOOT_VALIDATED"), indent=2))
        print("Backup verified locally. Gate 0 still requires probe boot and restoration evidence.")
        return
    record = json.loads(receipt.read_text())
    if sha(full.read_bytes()) != record["flash_sha256"]:
        raise RuntimeError("Recovery backup hash mismatch")
    if args.action == "restore":
        # REVIEW: write otadata FIRST so a power loss mid-restore boots factory,
        # not a half-written probe in ota_0. Then restore ota_0 contents.
        for name in ("ota_0", "otadata"):
            path = folder / (name + ".bin")
            if sha(path.read_bytes()) != record["region_sha256"][name]:
                raise RuntimeError("Recovery region hash mismatch")
            run(args.port, "write_flash", hex(EXPECTED[name][0]), path)
            run(args.port, "verify_flash", hex(EXPECTED[name][0]), path)
        print("Original ota_0 and otadata restored. Verify original boot console.")
        return
    if not args.image or not 0 < args.image.stat().st_size <= 0x400000:
        parser.error("A probe application <=4 MiB is required")
    # Sole OTA slot: any valid sequence selects ota_0. No rollback assumptions.
    # REVIEW: confirm the probe .bin is an esp32s3 IDF 6.0.2 image (magic 0xE9,
    # chip_id 9) and <= 4 MiB before write. A 5.5 image here does not test Gate 0.
    # REVIEW: ota_state=2 is ESP_OTA_IMG_VALID. Fine with rollback disabled;
    # if the installed 5.5 bootloader was built WITH rollback, VALID skips
    # pending-verify and is still OK. NEW(0) + rollback would abort next boot.
    select = bytearray(b"\xff" * 8192)
    struct.pack_into("<I20sII", select, 0, 1, b"\xff" * 20, 2,
                     zlib.crc32(struct.pack("<I", 1), 0xFFFFFFFF))
    selector = folder / "probe-otadata.bin"
    selector.write_bytes(select)
    run(args.port, "write_flash", "0x500000", args.image)
    run(args.port, "verify_flash", "0x500000", args.image)
    run(args.port, "write_flash", "0xf000", selector)
    print("Probe selected. Require WS183_GATE0_PASS and restore before recording Gate 0 PASS.")


if __name__ == "__main__":
    main()

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
from flash_ota0 import read_mac, check_image

ROOT = Path(__file__).resolve().parents[2]
EXPECTED = {"nvs": (0x9000, 0x6000), "otadata": (0xF000, 0x2000),
            "phy_init": (0x11000, 0x1000), "model": (0x12000, 952 * 1024),
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
        # seq=1 was boot-tested with the original bootloader; the probe also
        # reports the ROM CRC so the independent Python test checks that vector.
        valid = seq not in (0, 0xFFFFFFFF) and crc == zlib.crc32(struct.pack("<I", seq), 0xFFFFFFFF)
        entries.append(dict(sequence=seq, state=state, valid=valid))
    return entries


def run(port, *args):
    subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port,
                    "--baud", "460800", *map(str, args)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["backup", "probe", "restore"])
    parser.add_argument("--port", required=True)
    parser.add_argument("--expect-mac", default="44:1B:F6:84:DA:88")
    parser.add_argument("--backup", type=Path, required=True)
    parser.add_argument("--image", type=Path)
    args = parser.parse_args()
    if read_mac(args.port) != args.expect_mac.upper():
        parser.error("Board identity mismatch; no flash write performed")
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
        # This deployment requires original factory boot. Restore selection
        # first so interrupted ota_0 restoration cannot boot a partial image.
        if any(e['valid'] for e in record['otadata']):
            raise RuntimeError('Original boot was OTA; factory recovery route not established')
        for name in ("otadata", "ota_0"):
            path = folder / (name + ".bin")
            if sha(path.read_bytes()) != record["region_sha256"][name]:
                raise RuntimeError("Recovery region hash mismatch")
            run(args.port, "write_flash", hex(EXPECTED[name][0]), path)
            run(args.port, "verify_flash", hex(EXPECTED[name][0]), path)
        print("Original ota_0 and otadata restored. Verify original boot console.")
        return
    if not args.image or not 0 < args.image.stat().st_size <= 0x400000:
        parser.error("A probe application <=4 MiB is required")
    check_image(args.image)
    image_data=args.image.read_bytes()
    # esp_app_desc_t begins after 24-byte image + 8-byte first segment header.
    if image_data[144:176].split(b'\0',1)[0] != b'v6.0.2':
        parser.error('Gate 0/probe requires an application built with IDF v6.0.2')
    # Sole OTA slot: any valid sequence selects ota_0. No rollback assumptions.
    # ESP_OTA_IMG_VALID, no assumption of automatic rollback.
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

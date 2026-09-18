"""Authoritative package receipt: package bytes, JS bytes, budgets, profile."""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

JS, PAK = 3, 4


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sections(data: bytes) -> dict[int, bytes]:
    magic, version, manifest_len, variant_count = struct.unpack_from("<IIII", data, 0)
    if magic != 0x544B4350 or version != 1 or variant_count < 1:
        raise ValueError("not a PocketJS v1 package")
    manifest_pad = (manifest_len + 15) & ~15
    target, abi, nsec, off, _res, _hash = struct.unpack_from("<16sIIIIQ", data, 16 + manifest_pad)
    found = {}
    for i in range(nsec):
        kind, _r, start, length = struct.unpack_from("<IIII", data, off + i * 16)
        found[kind] = data[start:start + length]
    return {"target": target.split(b"\0", 1)[0].decode(), "abi": abi, "sections": found}


def js_bytes(blob: bytes) -> int:
    if not blob:
        return 0
    return len(blob) - 1 if blob[-1] == 0 else len(blob)


def receipt_for(path: Path, *, transform: str, original: Path | None, budgets: dict, profile_id: str) -> dict:
    data = path.read_bytes()
    info = sections(data)
    sec = info["sections"]
    js = sec.get(JS, b"")
    pak = sec.get(PAK, b"")
    rec = {
        "variant": None,
        "profileId": profile_id,
        "targetId": info["target"],
        "hostAbi": info["abi"],
        "transform": transform,
        "packageBytes": len(data),
        "packageSha256": sha(data),
        "javascriptBytes": js_bytes(js),
        "javascriptSha256": sha(js) if js else None,
        "pakBytes": len(pak),
        "budgets": budgets,
        "recovered": True,
        "originalMissing": False,
    }
    if original is not None:
        if original.is_file():
            raw = original.read_bytes()
            ojs = sections(raw)["sections"].get(JS, b"")
            rec.update(
                originalPackageBytes=len(raw),
                originalPackageSha256=sha(raw),
                originalJavascriptBytes=js_bytes(ojs),
            )
        else:
            rec["originalMissing"] = True
    return rec


def default_budgets(root: Path) -> dict:
    kconfig = (root / "main/Kconfig.projbuild").read_text(encoding="utf-8")
    defn = {"heapBytes": 1048576, "jsStackBytes": 24576, "evalBudgetUs": 500000, "turnBudgetUs": 20000}
    for key, token in (
        ("heapBytes", "HARNESS_HEAP_BYTES"),
        ("jsStackBytes", "HARNESS_JS_STACK_BYTES"),
        ("evalBudgetUs", "HARNESS_EVAL_BUDGET_US"),
        ("turnBudgetUs", "HARNESS_TURN_BUDGET_US"),
    ):
        marker = f"config {token}"
        i = kconfig.find(marker)
        if i < 0:
            continue
        chunk = kconfig[i:i + 200]
        if "default " in chunk:
            defn[key] = int(chunk.split("default ", 1)[1].split()[0])
    return defn


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("package", type=Path)
    p.add_argument("--transform", default="none")
    p.add_argument("--original", type=Path)
    p.add_argument("--variant")
    p.add_argument("--out", type=Path)
    args = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    profile = json.loads((root / "pocket.host.json").read_text(encoding="utf-8"))
    rec = receipt_for(args.package, transform=args.transform, original=args.original,
                      budgets=default_budgets(root), profile_id=profile["id"])
    rec["variant"] = args.variant
    rec["bun"] = None
    dest = args.out or Path(str(args.package) + ".receipt.json")
    dest.write_text(json.dumps(rec, indent=2) + "\n", encoding="utf-8")
    print(dest)


if __name__ == "__main__":
    main()

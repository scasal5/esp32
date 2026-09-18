"""Generate trusted host facts from OUR profile, never from an untrusted package."""
import hashlib
import json
from pathlib import Path
import sys


def generate(profile):
    digest = hashlib.sha256(json.dumps(profile, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()).digest()
    assert profile["id"] == "ws183-harness"
    assert profile["display"]["physicalViewport"] == [240, 284]
    # Official IDF adapter ABI at the pinned PocketJS revision.
    return '''#pragma once
#include "pocketjs/package.h"
static const pocketjs_package_host_contract_t ws_contract = {
 .struct_size=sizeof(pocketjs_package_host_contract_t),.target_id="ws183-harness",
 .host_abi=1,.tick_hz=30,.logical_width=240,.logical_height=284,
 .physical_width=240,.physical_height=284,.raster_density=1,
 .presentation=POCKETJS_PRESENTATION_NATIVE,.profile_hash={%s}
};
''' % ",".join(str(b) for b in digest)


if __name__ == "__main__":
    Path(sys.argv[2]).write_text(generate(json.loads(Path(sys.argv[1]).read_text())), encoding="utf-8")

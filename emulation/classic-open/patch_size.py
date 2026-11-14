#!/usr/bin/env python3
"""Let Classic Open Helper run in the background.

Retro68's application template marks apps cannotBackgound. A Startup Item
helper must be allowed to run behind the Finder. This rewrites the SIZE
resource in the generated .rsrc/ClassicOpenHelper.APPL (the resource fork
SheepShaver reads beside the .APPL data fork), clearing that one flag.

    python3 patch_size.py build/.../.rsrc/ClassicOpenHelper.APPL
"""

import struct
import sys
from pathlib import Path

CANNOT_BACKGROUND = 0x1000
SIZE_ID = 0xFFFF


def patch(path):
    data = bytearray(Path(path).read_bytes())
    data_offset, map_offset, _, _ = struct.unpack(">IIII", data[:16])
    type_list = map_offset + struct.unpack(">H", data[map_offset + 24:map_offset + 26])[0]
    types = struct.unpack(">H", data[type_list:type_list + 2])[0] + 1
    for i in range(types):
        entry = type_list + 2 + 8 * i
        kind = bytes(data[entry:entry + 4])
        count = struct.unpack(">H", data[entry + 4:entry + 6])[0] + 1
        ref_list = struct.unpack(">H", data[entry + 6:entry + 8])[0]
        for j in range(count):
            ref = type_list + ref_list + 12 * j
            resource_id = struct.unpack(">H", data[ref:ref + 2])[0]
            resource_offset = int.from_bytes(data[ref + 5:ref + 8], "big")
            if kind == b"SIZE" and resource_id == SIZE_ID:
                base = data_offset + resource_offset + 4
                flags = struct.unpack(">H", data[base:base + 2])[0]
                data[base:base + 2] = struct.pack(">H", flags & ~CANNOT_BACKGROUND)
                Path(path).write_bytes(data)
                print(f"patched {path}: SIZE flags {flags:#06x} -> {flags & ~CANNOT_BACKGROUND:#06x}")
                return
    raise SystemExit(f"{path}: no SIZE (-1) resource")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    patch(sys.argv[1])

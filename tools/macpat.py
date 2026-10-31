#!/usr/bin/env python3
"""Decode Mac OS 9 pattern resources to RGBA tiles.

`PAT ` (8-byte) and `PAT#` are 8x8 one-bit patterns; `ppat` is a pixel
pattern, usually an indexed colour tile with its own colour table.

Nothing from Apple ships: this only decodes the user's own extracted
resources on their machine.
"""

import struct


def decode_pat(data, ink=(0, 0, 0), paper=(255, 255, 255)):
    """Decode an 8-byte 8x8 one-bit pattern into rows of (r, g, b, a)."""
    if len(data) < 8:
        raise ValueError("pattern is too short")
    rows = []
    for y in range(8):
        row = []
        for x in range(8):
            on = bool(data[y] & (0x80 >> x))
            row.append((ink[0], ink[1], ink[2], 255) if on
                       else (paper[0], paper[1], paper[2], 255))
        rows.append(row)
    return rows


def decode_ppat(data):
    """Decode a `ppat` pixel pattern to (width, height, rows of (r, g, b, a)).

    The resource holds a PixPat header (28 bytes: type, then offsets to the
    PixMap and its pixel data), the PixMap, and its colour table. Old-style
    patterns (type 0) carry an 8x8 one-bit pattern at offset 20. Pixel rows
    are padded to the row size, which bounds the width."""
    if len(data) < 28:
        raise ValueError("ppat is too short")
    pat_type = struct.unpack(">H", data[0:2])[0]
    if pat_type == 0:
        return 8, 8, decode_pat(data[20:28])
    if pat_type != 1:
        raise ValueError("unsupported ppat type %d" % pat_type)
    pixmap = struct.unpack(">I", data[2:6])[0]
    pixels = struct.unpack(">I", data[6:10])[0]
    if pixmap + 50 > len(data):
        raise ValueError("ppat PixMap is outside the resource")
    row_bytes = struct.unpack(">H", data[pixmap + 4:pixmap + 6])[0] & 0x3FFF
    top, left, bottom, right = struct.unpack(">hhhh", data[pixmap + 6:pixmap + 14])
    depth = struct.unpack(">H", data[pixmap + 32:pixmap + 34])[0]
    table = struct.unpack(">I", data[pixmap + 42:pixmap + 46])[0]
    if depth not in (1, 2, 4, 8) or row_bytes == 0:
        raise ValueError("unsupported ppat depth %d" % depth)
    width = min(right - left, row_bytes * 8 // depth)
    height = bottom - top
    if height <= 0 or pixels + row_bytes * height > len(data):
        raise ValueError("ppat pixel data is outside the resource")
    palette = _colour_table(data, table)
    mask = (1 << depth) - 1
    rows = []
    for y in range(height):
        row = []
        for x in range(width):
            byte = data[pixels + y * row_bytes + (x * depth) // 8]
            index = (byte >> (8 - depth - (x * depth) % 8)) & mask
            r, g, b = palette.get(index, (0, 0, 0))
            row.append((r, g, b, 255))
        rows.append(row)
    return width, height, rows


def _colour_table(data, off):
    """Map each colour-table value to its RGB (a 16-bit ColorTable)."""
    if off + 8 > len(data):
        raise ValueError("colour table is outside the resource")
    size = struct.unpack(">H", data[off + 6:off + 8])[0]
    palette = {}
    for k in range(size + 1):
        at = off + 8 + 8 * k
        if at + 8 > len(data):
            raise ValueError("colour table is truncated")
        value, r, g, b = struct.unpack(">HHHH", data[at:at + 8])
        palette[value] = (r >> 8, g >> 8, b >> 8)
    return palette


def rgba_bytes(rows):
    """Flatten rows of (r, g, b, a) into a bytearray of RGBA."""
    size = len(rows)
    out = bytearray(size * size * 4)
    p = 0
    for row in rows:
        for r, g, b, a in row:
            out[p:p + 4] = bytes((r, g, b, a))
            p += 4
    return out


if __name__ == "__main__":
    import sys
    print("This module decodes Mac OS 9 patterns; it is not run directly.")
    sys.exit(1)
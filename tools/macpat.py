#!/usr/bin/env python3
"""Decode Mac OS 9 pattern resources to RGBA tiles.

`PAT ` (8-byte) and `PAT#` are 8x8 one-bit patterns; `ppat` is a colour
pixel pattern (a PixMap tile with its own colour table, as the System
file's own desktop ppats show). Pixel data is unpacked or PackBits-packed;
old-style (type 0) patterns carry an 8x8 one-bit pattern.

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


def _unpack_packbits(data, stride, height):
    """Unpack PackBits rows (each exactly `stride` bytes) from data."""
    rows = []
    pos = 0
    for _ in range(height):
        row = bytearray()
        while len(row) < stride:
            if pos >= len(data):
                raise ValueError("ppat pixels end mid-row")
            n = struct.unpack("b", data[pos:pos + 1])[0]
            pos += 1
            if n >= 0:
                row += data[pos:pos + n + 1]
                pos += n + 1
            elif n != -128:
                if pos >= len(data):
                    raise ValueError("ppat pixels end mid-run")
                row += bytes((data[pos],)) * (1 - n)
                pos += 1
        if len(row) != stride:
            raise ValueError("ppat row is not its stride")
        rows.append(bytes(row))
    return rows, pos


def decode_ppat(data):
    """Decode a `ppat` pixel-pattern resource to (width, height, rows of
    (r, g, b, a)).

    Layout (the System file's desktop ppats): a type word (1 = pixmap),
    the pixel-data offset at 6, the 1-bit fallback at 20, a 46-byte PixMap
    at 32 and the colour table at its absolute pmTable offset.
    Raises ValueError when the pattern is not a plain indexed/direct
    pixmap the desktop can tile.
    """
    if len(data) < 28:
        raise ValueError("ppat is too short")
    pat_type = struct.unpack(">H", data[0:2])[0]
    if pat_type == 0:
        return 8, 8, decode_pat(data[20:28])
    if len(data) < 78:
        raise ValueError("ppat is too short")
    data_off = struct.unpack(">I", data[6:10])[0]
    row_bytes, top, left, bottom, right, _ver, pack, pack_size = \
        struct.unpack(">H4HHHI", data[32:50])
    _hres, _vres, pixel_type, pixel_size, cmp_count, _cmp_size, _planes, pm_table, _rsv = \
        struct.unpack(">IIHHHHIII", data[50:78])
    if not row_bytes & 0x8000:
        raise ValueError("ppat is not a pixmap")
    stride = row_bytes & 0x3FFF
    width, height = right - left, bottom - top
    if not 0 < width <= 512 or not 0 < height <= 512 or stride <= 0 or stride > 4096:
        raise ValueError("ppat has no usable geometry")
    if pixel_size in (1, 2, 4, 8):
        if cmp_count != 1:
            raise ValueError("ppat components are not indexed")
    elif pixel_size in (16, 32):
        if cmp_count != 3:
            raise ValueError("ppat components are not direct")
    else:
        raise ValueError("unsupported ppat depth %d" % pixel_size)
    if not 0 < data_off <= len(data):
        raise ValueError("ppat pixels are outside")
    if pack == 0:
        if data_off + height * stride > len(data):
            raise ValueError("ppat pixels run past the end")
        raw = [data[data_off + y * stride:data_off + (y + 1) * stride] for y in range(height)]
    elif pack in (1, 2):
        if data_off + pack_size > len(data):
            raise ValueError("ppat pixels run past the end")
        raw, _used = _unpack_packbits(data[data_off:data_off + pack_size], stride, height)
    else:
        raise ValueError("unsupported ppat packing %d" % pack)
    table = {}
    if pixel_size <= 8:
        if pm_table + 8 > len(data):
            raise ValueError("ppat colour table is outside")
        count = struct.unpack(">H", data[pm_table + 6:pm_table + 8])[0] + 1
        if count <= 0 or count > 256 or pm_table + 8 + 8 * count > len(data):
            raise ValueError("ppat colour table is invalid")
        for i in range(count):
            idx = struct.unpack(">H", data[pm_table + 8 + 8 * i:pm_table + 10 + 8 * i])[0]
            r = struct.unpack(">H", data[pm_table + 10 + 8 * i:pm_table + 12 + 8 * i])[0] >> 8
            g = struct.unpack(">H", data[pm_table + 12 + 8 * i:pm_table + 14 + 8 * i])[0] >> 8
            b = struct.unpack(">H", data[pm_table + 14 + 8 * i:pm_table + 16 + 8 * i])[0] >> 8
            table[idx] = (r, g, b)
    rows = []
    for y in range(height):
        row = []
        line = raw[y]
        if pixel_size == 32:
            for x in range(width):
                row.append((line[4 * x + 1], line[4 * x + 2], line[4 * x + 3], 255))
        elif pixel_size == 16:
            for x in range(width):
                v = struct.unpack(">H", line[2 * x:2 * x + 2])[0]
                row.append((((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31,
                    (v & 31) * 255 // 31, 255))
        else:
            for x in range(width):
                bit = x * pixel_size
                v = (line[bit // 8] >> (8 - pixel_size - bit % 8)) & ((1 << pixel_size) - 1)
                r, g, b = table.get(v, (0, 0, 0))
                row.append((r, g, b, 255))
        rows.append(row)
    return width, height, rows


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
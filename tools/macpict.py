#!/usr/bin/env python3
"""Decode Mac OS 9 PICT pictures to RGBA.

PICT is QuickDraw's picture format: a 512-byte header followed by a stream of
opcodes. Desktop pictures (the theme's wallpapers) are usually a 1-bit BitMap
(opcodes 0x0090 BitsRect / 0x0098 PackBitsRect) or a colour PixMap (0x009A).
This handles the 1-bit bitmap cases and PackBits; colour PixMaps are a
follow-up.

Nothing from Apple ships: this only decodes the user's own extracted
resources on their machine.
"""

import struct


def packbits_decode(data, expected):
    """Decompress Apple PackBits into `expected` bytes."""
    out = bytearray()
    i = 0
    n = len(data)
    while i < n and len(out) < expected:
        c = data[i]
        i += 1
        if c & 0x80:
            count = 257 - c  # 1..128 repeats of the next byte
            if i < n:
                out += bytes((data[i],)) * count
                i += 1
        elif c < 0x80:
            count = c + 1  # 1..128 literal bytes
            out += data[i:i + count]
            i += count
        # c == 0x80 is a no-op
    return bytes(out[:expected])


def _bits_to_rows(pixels, width, height, rowbytes):
    rows = []
    for y in range(height):
        row = []
        for x in range(width):
            b = pixels[y * rowbytes + x // 8]
            on = bool(b & (0x80 >> (x % 8)))
            row.append((0, 0, 0, 255) if on else (255, 255, 255, 255))
        rows.append(row)
    return rows


def decode_pict(data):
    """Decode a PICT to (width, height, rows of (r,g,b,a))."""
    if len(data) < 522:
        raise ValueError("PICT is too short for its header")
    top, left, bottom, right = struct.unpack(">hhhh", data[2:10])
    width = right - left
    height = bottom - top
    if width <= 0 or height <= 0:
        raise ValueError("PICT bounding box is invalid")

    # Opcodes begin after the 512-byte header.
    off = 512
    end = len(data)
    while off + 2 <= end:
        op = struct.unpack(">H", data[off:off + 2])[0]
        if op == 0x00FF:  # end of picture
            break
        if op in (0x0090, 0x0098):  # BitsRect / PackBitsRect, 1-bit
            rowbytes = struct.unpack(">H", data[off + 2:off + 4])[0]
            if rowbytes & 0x8000:
                raise ValueError("PICT PixMap variant is not yet supported")
            bounds = struct.unpack(">hhhh", data[off + 4:off + 12])
            bw = bounds[3] - bounds[1]  # right - left
            bh = bounds[2] - bounds[0]  # bottom - top
            packed = (op == 0x0098)
            body = data[off + 12:]
            if packed:
                pixels = packbits_decode(body, rowbytes * bh)
            else:
                pixels = body[:rowbytes * bh]
            return bw, bh, _bits_to_rows(pixels, bw, bh, rowbytes)
        if op == 0x009A:  # direct PixMap (colour) — follow-up
            raise ValueError("PICT colour PixMap is not yet supported")
        # Unknown opcode: stop — the common cases come first.
        break
    raise ValueError("PICT has no decodable bitmap")


if __name__ == "__main__":
    import sys
    print("This module decodes Mac OS 9 PICT pictures; it is not run directly.")
    sys.exit(1)
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


def decode_pixmap(body, clut=None):
    """Decode a colour PixMap (opcode 0x009A body) into rows of RGBA.

    Handles 32-bit and 24-bit direct, 16-bit 5-5-5, and 8-bit indexed with a
    colour table; both PackBits and unpacked rows."""
    if len(body) < 50:
        raise ValueError("PixMap is too short")
    # After the opcode: baseAddr(4), rowBytes(2), bounds(8), pmVersion(2),
    # packType(2), packSize(4), hRes(4), vRes(4), pixelType(2), pixelSize(2),
    # cmpCount(2), cmpSize(2), planeBytes(4), pmTable(4), pmReserved(4).
    rowbytes = struct.unpack(">H", body[4:6])[0] & 0x3FFF
    bounds = struct.unpack(">hhhh", body[6:14])
    width = bounds[3] - bounds[1]  # right - left
    height = bounds[2] - bounds[0]  # bottom - top
    pack_type = struct.unpack(">H", body[16:18])[0]
    pixel_size = struct.unpack(">H", body[32:34])[0]
    off = 50
    local_clut = None
    if pixel_size in (1, 2, 4, 8):
        if off + 8 <= len(body):
            ct_size = struct.unpack(">H", body[off + 6:off + 8])[0]
            colours = []
            p = off + 8
            for i in range(ct_size + 1):
                idx = struct.unpack(">H", body[p:p + 2])[0]
                r = struct.unpack(">H", body[p + 2:p + 4])[0] >> 8
                g = struct.unpack(">H", body[p + 4:p + 6])[0] >> 8
                b = struct.unpack(">H", body[p + 6:p + 8])[0] >> 8
                colours.append((idx, (r, g, b)))
                p += 8
            local_clut = colours
            off = p
    lookup = local_clut or clut
    raw = body[off:]
    expected = rowbytes * height
    if pack_type == 1:
        pixels = packbits_decode(raw, expected)
    else:
        pixels = raw[:expected]
    return _pixmap_to_rows(pixels, width, height, rowbytes, pixel_size, lookup)


def _pixmap_to_rows(pixels, width, height, rowbytes, pixel_size, lookup):
    rows = []
    for y in range(height):
        row = []
        base = y * rowbytes
        for x in range(width):
            if pixel_size == 32:
                # xRGB or ARGB, big-endian; alpha is usually 0 -> opaque.
                r, g, b, a = pixels[base + x * 4:base + x * 4 + 4]
                if a == 0:
                    a = 255
                row.append((r, g, b, a))
            elif pixel_size == 24:
                r, g, b = pixels[base + x * 3:base + x * 3 + 3]
                row.append((r, g, b, 255))
            elif pixel_size == 16:
                v = struct.unpack(">H", pixels[base + x * 2:base + x * 2 + 2])[0]
                r = (v >> 10) & 0x1F
                g = (v >> 5) & 0x1F
                b = v & 0x1F
                row.append((r * 255 // 31, g * 255 // 31, b * 255 // 31, 255))
            elif pixel_size == 8:
                idx = pixels[base + x]
                rgb = lookup[idx][1] if lookup and idx < len(lookup) else (idx, idx, idx)
                row.append((rgb[0], rgb[1], rgb[2], 255))
            else:
                raise ValueError("unsupported pixel size %d" % pixel_size)
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
            bw = bounds[3] - bounds[1]
            bh = bounds[2] - bounds[0]
            packed = (op == 0x0098)
            body = data[off + 12:]
            if packed:
                pixels = packbits_decode(body, rowbytes * bh)
            else:
                pixels = body[:rowbytes * bh]
            return bw, bh, _bits_to_rows(pixels, bw, bh, rowbytes)
        if op == 0x009A:  # direct PixMap (colour)
            body = data[off + 2:]
            rows = decode_pixmap(body)
            return len(rows[0]), len(rows), rows
        # Unknown opcode: stop — the common cases come first.
        break
    raise ValueError("PICT has no decodable bitmap")


if __name__ == "__main__":
    import sys
    print("This module decodes Mac OS 9 PICT pictures; it is not run directly.")
    sys.exit(1)
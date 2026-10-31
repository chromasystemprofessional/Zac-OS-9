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


def packbits_decode(data, expected, unit=1):
    """Decompress Apple PackBits into `expected` bytes. `unit` is the size of
    one run element: 1 for bytes, 2 or 4 for the 16- and 32-bit pixel packings."""
    out = bytearray()
    i = 0
    n = len(data)
    if unit > 1:
        while i < n and len(out) < expected:
            c = data[i]
            i += 1
            if c & 0x80:
                count = 257 - c
                if i + unit <= n:
                    out += data[i:i + unit] * count
                    i += unit
            else:
                size = (c + 1) * unit
                out += data[i:i + size]
                i += size
        return bytes(out[:expected])
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


# Version 2 opcode data sizes after the opcode, for the ones a picture can
# carry before its bitmap. Region and polygon opcodes carry their own size.
V2_FIXED = {
    0x0000: 0, 0x0002: 8, 0x0003: 2, 0x0004: 2, 0x0005: 2, 0x0006: 4, 0x0007: 4,
    0x0008: 2, 0x0009: 8, 0x000A: 8, 0x000B: 4, 0x000C: 4, 0x000D: 2, 0x000E: 4,
    0x000F: 4, 0x0010: 8, 0x0011: 2, 0x0015: 2, 0x0016: 2, 0x001A: 6, 0x001B: 6,
    0x001C: 0, 0x001D: 6, 0x001E: 0, 0x001F: 6, 0x0020: 8, 0x0021: 4, 0x0022: 4,
    0x0023: 2, 0x00A0: 2,
}
V2_BITMAPS = (0x0090, 0x0091, 0x0098, 0x0099, 0x009A, 0x009B)
V2_SIZED = (0x0001,)  # clip region: its size word counts the whole region
V2_RECTS = tuple(range(0x0030, 0x0035)) + tuple(range(0x0050, 0x0055)) + (
    tuple(range(0x0038, 0x003D)) + tuple(range(0x0058, 0x005D)))
V2_SAME = tuple(range(0x0038, 0x003D)) + tuple(range(0x0058, 0x005D))


def _u16(data, pos):
    return struct.unpack(">H", data[pos:pos + 2])[0]


def _skip_opcode(op, data, pos):
    """Return the position after one version 2 opcode's data."""
    if op == 0x0C00:  # header
        return pos + 24
    if op in V2_FIXED:
        return pos + V2_FIXED[op]
    if op in V2_SIZED or 0x0040 <= op <= 0x0047 or 0x0070 <= op <= 0x0077:
        return pos + _u16(data, pos)
    if op in V2_SAME:
        return pos
    if op in V2_RECTS:
        return pos + 8
    if op == 0x00A1:  # long comment: kind, size, data padded to a word
        size = _u16(data, pos + 2)
        return pos + 4 + size + (size & 1)
    raise ValueError("unsupported PICT opcode 0x%04x" % op)


def _packed_rows(data, pos, height, rowbytes, packed, unit=1):
    """Read `height` rows of pixel data, each PackBits-compressed when it is
    at least 8 bytes wide; returns (pixels, next position)."""
    pixels = bytearray()
    for _ in range(height):
        if packed and rowbytes >= 8:
            if rowbytes > 250:
                count = _u16(data, pos)
                pos += 2
            else:
                count = data[pos]
                pos += 1
            pixels += packbits_decode(data[pos:pos + count], rowbytes, unit)
            pos += count
        else:
            pixels += data[pos:pos + rowbytes]
            pos += rowbytes
    return bytes(pixels), pos


def _colour_table(data, pos):
    """Read an inline colour table; returns (lookup, next position)."""
    size = _u16(data, pos + 6)
    colours = []
    p = pos + 8
    for _ in range(size + 1):
        idx = _u16(data, p)
        r, g, b = (_u16(data, p + 2 + 2 * k) >> 8 for k in range(3))
        colours.append((idx, (r, g, b)))
        p += 8
    return colours, p


def _v2_bitmap(data, pos):
    """Decode the first bitmap opcode of a version 2 picture."""
    while pos + 2 <= len(data):
        op = _u16(data, pos)
        pos += 2
        if op == 0x00FF:
            break
        if op in V2_BITMAPS:
            return _v2_bitmap_body(op, data, pos)
        pos = _skip_opcode(op, data, pos)
    raise ValueError("PICT has no decodable bitmap")


def _v2_bitmap_body(op, data, pos):
    """Decode a BitsRect/BitsRgn (1-bit) or DirectBitsRect/Rgn (PixMap) body."""
    if op in (0x009A, 0x009B):
        pixmap = data[pos:pos + 50]
        if len(pixmap) < 50:
            raise ValueError("PixMap is truncated")
        rowbytes = struct.unpack(">H", pixmap[4:6])[0] & 0x3FFF
        top, left, bottom, right = struct.unpack(">hhhh", pixmap[6:14])
        pack_type = struct.unpack(">H", pixmap[16:18])[0]
        pixel_size = struct.unpack(">H", pixmap[32:34])[0]
        width, height = right - left, bottom - top
        pos += 50
        lookup = None
        if pixel_size <= 8:
            lookup, pos = _colour_table(data, pos)
        pos += 18  # source rect, destination rect, transfer mode
    else:
        rowbytes = _u16(data, pos) & 0x3FFF
        top, left, bottom, right = struct.unpack(">hhhh", data[pos + 2:pos + 10])
        width, height = right - left, bottom - top
        pixel_size, pack_type, lookup = 1, 0, None
        pos += 10 + 18
    if width <= 0 or height <= 0 or width > 4096 or height > 4096:
        raise ValueError("PICT bitmap size is invalid")
    if op in (0x009B, 0x0091, 0x0099):
        pos += _u16(data, pos)  # region
    if op in (0x009A, 0x009B):
        packed = pack_type != 1
        # Pixel rows pack whole 16- or 32-bit pixels (packType 3 and 4).
        unit = 2 if pixel_size == 16 and pack_type == 3 else 4 if pixel_size == 32 and pack_type == 4 else 1
    else:
        packed = op in (0x0098, 0x0099)
        unit = 1
    pixels, _ = _packed_rows(data, pos, height, rowbytes, packed, unit)
    if pixel_size == 1:
        return width, height, _bits_to_rows(pixels, width, height, rowbytes)
    return width, height, _pixmap_to_rows(pixels, width, height, rowbytes, pixel_size, lookup)


def _decode_v2(data):
    """Decode a version 2 PICT in resource form (no 512-byte file header)."""
    pos = 14  # after the size, bounds and version opcodes
    if _u16(data, pos) != 0x0C00:
        raise ValueError("not a version 2 PICT")
    return _v2_bitmap(data, pos + 2 + 24)


def decode_pict(data):
    """Decode a PICT to (width, height, rows of (r,g,b,a)).

    Accepts a resource (PICT body) or a file with its 512-byte header."""
    if len(data) > 512 and data[:512] == bytes(512):
        data = data[512:]
    if len(data) >= 14 and data[10:12] == b"\x00\x11" and data[12:14] == b"\x02\xff":
        width, height, rows = _decode_v2(data)
        return width, height, rows
    return _decode_v1(data)


def _decode_v1(data):
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
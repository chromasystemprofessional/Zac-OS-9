#!/usr/bin/env python3
"""Write RGBA rows as a PNG, and scale them (stdlib only, no Pillow).

The theme extractor needs this to turn decoded Mac OS 9 icons into the
menu-icon.png and boot art the compositor reads.
"""

import struct
import zlib


def write_png(path, width, height, rows):
    """Write rows of (r, g, b, a) to a PNG file.

    `rows` is a list of `height` rows, each a flat sequence of `width`
    4-tuples (or any iterable of RGBA bytes)."""
    def scanline(row):
        flat = bytearray()
        for px in row:
            flat += bytes(px)
        return b"\x00" + bytes(flat)

    raw = b"".join(scanline(r) for r in rows)

    def chunk(typ, data):
        out = struct.pack(">I", len(data)) + typ + data
        out += struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF)
        return out

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)  # 8-bit RGBA
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)
    return path


def flat_rgba(rows):
    """Flatten rows of (r, g, b, a) into a single bytes object of RGBA."""
    out = bytearray()
    for row in rows:
        for r, g, b, a in row:
            out += bytes((r, g, b, a))
    return bytes(out)


def nearest_scale(rows, sw, sh, dw, dh):
    """Nearest-neighbour scale rows from sw x sh to dw x dh."""
    out = []
    for y in range(dh):
        sy = min(sh - 1, y * sh // dh)
        row = []
        for x in range(dw):
            sx = min(sw - 1, x * sw // dw)
            row.append(rows[sy][sx])
        out.append(row)
    return out


def fit_scale(rows, sw, sh, dw, dh, bg=(0, 0, 0, 0)):
    """Scale to fit inside dw x dh, centred, preserving aspect ratio."""
    scale = min(dw / sw, dh / sh)
    nw = max(1, int(sw * scale))
    nh = max(1, int(sh * scale))
    scaled = nearest_scale(rows, sw, sh, nw, nh)
    ox = (dw - nw) // 2
    oy = (dh - nh) // 2
    out = []
    for y in range(dh):
        row = []
        for x in range(dw):
            if ox <= x < ox + nw and oy <= y < oy + nh:
                row.append(scaled[y - oy][x - ox])
            else:
                row.append(bg)
        out.append(row)
    return out


if __name__ == "__main__":
    import sys
    print("This module writes PNGs; it is not run directly.")
    sys.exit(1)

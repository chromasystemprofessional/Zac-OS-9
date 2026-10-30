#!/usr/bin/env python3
"""Decode Mac OS 9 pattern resources to RGBA tiles.

`PAT ` (8-byte) and `PAT#` are 8x8 one-bit patterns; `ppat` is a colour
pattern. This decodes the 8x8 one-bit case; `ppat` is a follow-up.

Nothing from Apple ships: this only decodes the user's own extracted
resources on their machine.
"""


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
#!/usr/bin/env python3
"""Decode Mac OS 9 icon resources (icl8, icl4, ICN#, ics*) to RGBA.

These are the classic icon formats (Inside Macintosh: Icon Utilities):
  - icl8: 32x32, 8-bit indices into a colour table (clut), 1024 bytes.
  - icl4: 32x32, 4-bit indices (high nibble first), 512 bytes.
  - ICN#: 32x32, a 1-bit bitmap then a 1-bit mask, 256 bytes (128 + 128).
  - ics8/ics4/ics#: the same at 16x16 (256, 128, 64 bytes).

The extractor (tools/extract-macos9-theme.py) pulls these out of the
resource fork; this module turns the raw bytes into RGBA so they can be
written as PNG for the theme's menu icon and boot art.

Nothing from Apple ships: this only decodes the user's own extracted
resources on their machine.
"""

import struct


def clut_lookup(clut):
    """Turn a list of (index, (r, g, b)) into a list indexed by position.

    The ctEntry value field normally carries the index, but some tables
    (notably the System file's default 8-bit table) leave it 0 and rely on
    entry order, so fall back to positional order when every index is 0."""
    if not clut:
        return None
    indices = [idx for idx, _rgb in clut]
    if len(indices) > 2 and all(i == 0 for i in indices):
        return [rgb for _idx, rgb in clut]
    table = {}
    for idx, rgb in clut:
        table[idx] = rgb
    return [table[i] for i in sorted(table)]


def gray_ramp(n):
    """A gray ramp of n entries, used when an icon has no clut."""
    return [(int(255 * i / max(1, n - 1)),) * 3 for i in range(n)]


def decode_icl8(data, clut=None, size=32):
    """Decode an 8-bit indexed icon to a list of rows of (r, g, b, a)."""
    lookup = clut_lookup(clut)
    if lookup is None or len(lookup) < 256:
        # icl8 always indexes a full 256-entry table; fill with a ramp.
        ramp = gray_ramp(256)
        if lookup:
            ramp[:len(lookup)] = lookup
        lookup = ramp
    if len(data) < size * size:
        raise ValueError("icl8 is too short for %dx%d" % (size, size))
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            idx = data[y * size + x]
            r, g, b = lookup[idx]
            row.append((r, g, b, 255))
        rows.append(row)
    return rows


def decode_icl4(data, clut=None, size=32):
    """Decode a 4-bit indexed icon (two pixels per byte, high nibble first)."""
    lookup = clut_lookup(clut)
    if lookup is None or len(lookup) < 16:
        ramp = gray_ramp(16)
        if lookup:
            ramp[:len(lookup)] = lookup
        lookup = ramp
    if len(data) < (size * size + 1) // 2:
        raise ValueError("icl4 is too short for %dx%d" % (size, size))
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            byte = data[y * (size // 2) + x // 2]
            nib = (byte >> 4) if x % 2 == 0 else (byte & 0x0F)
            r, g, b = lookup[nib]
            row.append((r, g, b, 255))
        rows.append(row)
    return rows


def decode_icn1(data, size=32):
    """Decode an ICN#/ics# to (bitmap, mask): each a list of rows of bool.

    The bitmap is bit-set where the icon is black; the mask is bit-set where
    the icon is opaque. Bit 0x80 of each byte is the leftmost pixel."""
    bytes_per_row = size // 8
    bitmap_bytes = bytes_per_row * size
    if len(data) < bitmap_bytes * 2:
        raise ValueError("ICN# is too short for %dx%d" % (size, size))
    bitmap = []
    mask = []
    for y in range(size):
        brow = []
        mrow = []
        for x in range(size):
            b = data[y * bytes_per_row + x // 8]
            m = data[bitmap_bytes + y * bytes_per_row + x // 8]
            bit = 0x80 >> (x % 8)
            brow.append(bool(b & bit))
            mrow.append(bool(m & bit))
        bitmap.append(brow)
        mask.append(mrow)
    return bitmap, mask


def combine_icon(color_rows, mask, black=(0, 0, 0)):
    """Combine colour rows with a 1-bit mask into rows of (r, g, b, a)."""
    size = len(mask)
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            r, g, b, _ = color_rows[y][x]
            if mask[y][x]:
                row.append((r, g, b, 255))
            else:
                row.append((black[0], black[1], black[2], 0))
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
    print("This module decodes Mac OS 9 icon resources; it is not run directly.")
    sys.exit(1)
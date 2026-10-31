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


def _pixmap_fields(data, off=0):
	"""Parse a 50-byte PixMap, returning (rowBytes, bounds, pixelSize, cmpCount, pmTable)."""
	if off + 50 > len(data):
		raise ValueError("PixMap too short")
	rowBytes = struct.unpack(">H", data[off + 4:off + 6])[0] & 0x3FFF
	top, left, bottom, right = struct.unpack(">hhhh", data[off + 6:off + 14])
	pixelSize = struct.unpack(">H", data[off + 32:off + 34])[0]
	cmpCount = struct.unpack(">H", data[off + 34:off + 36])[0]
	pmTable = struct.unpack(">I", data[off + 42:off + 46])[0]
	return rowBytes, (right - left, bottom - top), pixelSize, cmpCount, pmTable


def _read_ctab(data, off):
	"""Read a ColorTable; return (lookup, next_offset)."""
	ctSeed, ctFlags, ctSize = struct.unpack(">IHH", data[off:off + 8])
	pos = off + 8
	entries = []
	for _ in range(ctSize + 1):
		val, r, g, b = struct.unpack(">HHHH", data[pos:pos + 8])
		entries.append((val, (r >> 8, g >> 8, b >> 8)))
		pos += 8
	lookup = clut_lookup(entries)
	return lookup, pos


def _indexed_rows(data, off, w, h, pixel_size, lookup):
	"""Decode an indexed bitmap (1/2/4/8-bit) to RGBA rows."""
	row_bytes = (w * pixel_size + 7) // 8
	need = 1 << pixel_size
	if lookup is None or len(lookup) < need:
		ramp = gray_ramp(need)
		if lookup:
			ramp[:len(lookup)] = lookup
		lookup = ramp
	rows = []
	for y in range(h):
		row = []
		for x in range(w):
			byte = data[off + y * row_bytes + (x * pixel_size) // 8]
			shift = 8 - pixel_size - ((x * pixel_size) % 8)
			idx = (byte >> shift) & ((1 << pixel_size) - 1)
			r, g, b = lookup[idx]
			row.append((r, g, b, 255))
		rows.append(row)
	return rows


def _pick_clut(cluts, pixel_size):
	"""Choose a palette for an indexed icon: exact depth first, then a
	larger table, then None."""
	if not cluts:
		return None
	need = 1 << pixel_size
	best = None
	for size, cols in cluts.items():
		if len(cols) >= need and (best is None or len(cols) < len(best)):
			best = cols
	return clut_lookup(best)


def _read_bitmap(data, off):
	"""Read a BitMap/PixMap header at `off`, returning (rowBytes, w, h, isPixmap)."""
	base = struct.unpack(">I", data[off:off + 4])[0]
	row_bytes = struct.unpack(">H", data[off + 4:off + 6])[0]
	top, left, bottom, right = struct.unpack(">hhhh", data[off + 6:off + 14])
	is_pixmap = bool(row_bytes & 0x8000)
	row_bytes &= 0x3FFF
	return row_bytes, (right - left, bottom - top), is_pixmap


def decode_cicn(data, clut=None, cluts=None):
	"""Decode a 'cicn' colour icon to RGBA rows.

	Layout (ResForge): PixMap(50) + mask BitMap(14) + bitmap BitMap(14) +
	a 4-byte handle + the mask data + the bitmap data (skipped) + the
	ColorTable + the colour pixel data."""
	row_bytes, (w, h), is_pixmap = _read_bitmap(data, 0)
	if not is_pixmap:
		raise ValueError("cicn has no PixMap")
	pixel_size = struct.unpack(">H", data[32:34])[0]
	# mask BitMap (14 bytes) follows the 50-byte PixMap, then the bitmap BitMap.
	mask_rb, (_mw, _mh), _ = _read_bitmap(data, 50)
	bit_rb, (_bw, _bh), _ = _read_bitmap(data, 64)
	mask_off = 82
	mask_len = mask_rb * h
	bit_off = mask_off + mask_len
	bit_len = bit_rb * h
	ctab_off = bit_off + bit_len
	lookup, pix_off = _read_ctab(data, ctab_off)
	if cluts:
		lookup = _pick_clut(cluts, pixel_size)
	elif clut:
		lookup = clut_lookup(clut)
	rows = _indexed_rows(data, pix_off, w, h, pixel_size, lookup)
	for y in range(h):
		for x in range(w):
			b = data[mask_off + y * mask_rb + x // 8]
			if not (b & (0x80 >> (x % 8))):
				rows[y][x] = (rows[y][x][0], rows[y][x][1], rows[y][x][2], 0)
	return rows


def decode_crsr(data, clut=None, cluts=None):
	"""Decode a 'crsr' colour cursor to (rows, hot_spot).

	Layout (QDCCrsr): a 96-byte header, then the PixMap, then the colour
	table (at the PixMap's pmTable offset) and pixel data."""
	if len(data) < 96:
		raise ValueError("crsr too short")
	crsr_type = struct.unpack(">H", data[0:2])[0]
	if crsr_type != 0x8001:
		raise ValueError("not a colour cursor")
	crsr_map = struct.unpack(">I", data[2:6])[0]
	crsr_data = struct.unpack(">I", data[6:10])[0]
	hot_y, hot_x = struct.unpack(">hh", data[84:88])
	row_bytes, (w, h), pixel_size, _cmp, pmTable = _pixmap_fields(data, crsr_map)
	lookup, _ = _read_ctab(data, pmTable)
	if cluts:
		lookup = _pick_clut(cluts, pixel_size)
	elif clut:
		lookup = clut_lookup(clut)
	rows = _indexed_rows(data, crsr_data, w, h, pixel_size, lookup)
	return rows, (hot_x, hot_y)


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
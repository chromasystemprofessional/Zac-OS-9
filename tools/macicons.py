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


# Screenshot-calibrated system palette entries: index -> (r, g, b).
# Mac icons index the ROM's default 8-bit system table, which survives in
# no resource fork; the file cluts are theme or photo tables that get some
# indices wrong (gray folders instead of periwinkle). Entries marked sampled
# were matched pixel-against-pixel with a running Mac OS 9 desktop and must
# keep working: the extractor's tests cover the folder, the caution sign
# and the Apple icon. The rest are the Mac's own certain whites and grays,
# or keep the lavender family's relative order (estimated).
CALIBRATED_SYSTEM_COLORS = {
    42: (204, 204, 255),  # sampled: desktop folder fill
    23: (255, 255, 0),  # sampled: caution-sign yellow
    5: (255, 255, 0),  # sampled: caution-sign dither pair
    245: (255, 255, 255),  # documents are white
    253: (221, 221, 221),  # trash body and other light-gray parts
    248: (192, 192, 192),  # trash and drive shading
    84: (153, 153, 221),  # folder back panel and side
    170: (170, 170, 238),  # folder edge detail
    213: (119, 119, 187),  # folder edge lines
}


def resolve_palette(cluts, calibrate=True):
    """The icon palette: the largest file clut, with calibrated system
    colours applied. Used for icons decoded from the user's own Mac OS 9
    install (never for the theme file's own icon, which keeps its gray
    ramp rule). Pass calibrate=False for art from a third-party theme
    file: Apple's samples describe Apple's icons, not its.
    """
    best = None
    for _res_id, cols in cluts:
        if cols and (best is None or len(cols) > len(best)):
            best = cols
    lookup = clut_lookup(best)
    if lookup is None:
        lookup = list(gray_ramp(256))
    else:
        lookup = list(lookup)
        while len(lookup) < 256:
            lookup.append(lookup[-1] if lookup else (0, 0, 0))
    if calibrate:
        for index, rgb in CALIBRATED_SYSTEM_COLORS.items():
            if 0 <= index < len(lookup):
                lookup[index] = rgb
    # As (index, rgb) pairs, so the decoders' clut_lookup keeps them.
    return list(enumerate(lookup))


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
	if clut:
		lookup = clut_lookup(clut)
	elif cluts and struct.unpack(">H", data[ctab_off + 6:ctab_off + 8])[0] < 1:
		lookup = _pick_clut(cluts, pixel_size)
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
	if clut:
		lookup = clut_lookup(clut)
	elif cluts and struct.unpack(">H", data[pmTable + 6:pmTable + 8])[0] < 1:
		lookup = _pick_clut(cluts, pixel_size)
	rows = _indexed_rows(data, crsr_data, w, h, pixel_size, lookup)
	return rows, (hot_x, hot_y)


def decode_curs(data):
	"""Decode a black-and-white 'CURS' to (rows, hot_spot).

	Layout: 16 rows of 16 bits (data), 16 rows of 16 bits (mask), then the
	hot spot as vertical then horizontal int16. A set mask bit with a set data
	bit is black, with a clear data bit white; a clear mask bit is transparent.
	"""
	if len(data) < 68:
		raise ValueError("CURS too short")
	hot_y, hot_x = struct.unpack(">hh", data[64:68])
	rows = []
	for y in range(16):
		row = []
		for x in range(16):
			byte = y * 2 + x // 8
			bit = 0x80 >> (x % 8)
			if not (data[32 + byte] & bit):
				row.append((0, 0, 0, 0))
			elif data[byte] & bit:
				row.append((0, 0, 0, 255))
			else:
				row.append((255, 255, 255, 255))
		rows.append(row)
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


def _unpack_icns_rgb(data, npixels):
    """Unpack an il32/is32 24-bit RLE stream into `npixels` (r, g, b) tuples."""
    if len(data) == npixels * 4:
        return [(data[i * 4 + 1], data[i * 4 + 2], data[i * 4 + 3]) for i in range(npixels)]
    pos = 4 if len(data) >= 4 and data[:4] == b"\x00\x00\x00\x00" else 0
    channels = [bytearray(), bytearray(), bytearray()]
    for ch in range(3):
        out = channels[ch]
        while len(out) < npixels and pos < len(data):
            b = data[pos]
            pos += 1
            if b & 0x80:
                run = b - 125
                if pos < len(data):
                    out.extend([data[pos]] * run)
                    pos += 1
            else:
                run = b + 1
                out.extend(data[pos:pos + run])
                pos += run
        if len(out) < npixels:
            out.extend([0] * (npixels - len(out)))
    return list(zip(channels[0][:npixels], channels[1][:npixels], channels[2][:npixels]))


def decode_icns(data, clut=None, size=32):
    """Decode a Mac OS 8.5+ 'icns' resource at `size` (32 or 16) to RGBA rows."""
    import struct
    if len(data) < 8 or data[:4] != b"icns":
        raise ValueError("not an icns resource")
    pos = 8
    chunks = {}
    while pos + 8 <= len(data):
        ctype = data[pos:pos + 4]
        clen = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        if clen < 8 or pos + clen > len(data):
            break
        chunks[ctype] = data[pos + 8:pos + clen]
        pos += clen
    rgb_tag = b"il32" if size == 32 else b"is32"
    mk8_tag = b"l8mk" if size == 32 else b"s8mk"
    c8_tag = b"icl8" if size == 32 else b"ics8"
    c4_tag = b"icl4" if size == 32 else b"ics4"
    m1_tag = b"ICN#" if size == 32 else b"ics#"
    npixels = size * size
    alpha = None
    if mk8_tag in chunks and len(chunks[mk8_tag]) >= npixels:
        alpha = chunks[mk8_tag][:npixels]
    elif m1_tag in chunks:
        _, mrows = decode_icn1(chunks[m1_tag], size=size)
        alpha = bytes(255 if mrows[y][x] else 0 for y in range(size) for x in range(size))
    if rgb_tag in chunks and alpha is not None:
        rgb = _unpack_icns_rgb(chunks[rgb_tag], npixels)
        rows = []
        for y in range(size):
            row = []
            for x in range(size):
                r, g, b = rgb[y * size + x]
                a = alpha[y * size + x]
                row.append((r, g, b, a) if a else (0, 0, 0, 0))
            rows.append(row)
        return rows
    if c8_tag in chunks and m1_tag in chunks:
        crows = decode_icl8(chunks[c8_tag], clut, size=size)
        _, mrows = decode_icn1(chunks[m1_tag], size=size)
        return combine_icon(crows, mrows)
    if c4_tag in chunks and m1_tag in chunks:
        crows = decode_icl4(chunks[c4_tag], clut, size=size)
        _, mrows = decode_icn1(chunks[m1_tag], size=size)
        return combine_icon(crows, mrows)
    raise ValueError("icns has no decodable %dx%d representation" % (size, size))


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
#!/usr/bin/env python3
"""Build a ZacOS theme bundle from a user-supplied Mac OS 9 tree.

Nothing from Apple ships with ZacOS: this tool only runs on the user's own
copy of Mac OS 9. It expects Old Dog to be set up already — the Mac OS 9
install (System Folder) is in ~/Classic, which is the default --source.

It walks the tree and, for each file, extracts artwork and sounds:
  - loose PNG/JPEG/ICNS images, .pat patterns and WAV/AIFF sounds are copied;
  - files with a resource fork (raw forks, or AppleDouble "._" files) have
    their icons, patterns, sounds and pictures pulled out.

Usage:
    tools/extract-macos9-theme.py --output ~/.config/zacos9/themes/MyTheme
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

# Find the decoder modules beside this script (source tree or installed).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import macicons
import macpat
import macpict
import macsnd
import pngwrite

# Loose files are copied straight across.
ASSET_EXTS = {".icns", ".png", ".jpg", ".jpeg", ".wav", ".aiff", ".aif", ".pat"}

# Resource-fork types and where they land.
ICON_TYPES = {b"ICN#", b"icl8", b"icl4", b"ics#", b"ics8", b"ics4", b"icns", b"ICON"}
PATTERN_TYPES = {b"PAT ", b"PAT#", b"ppat"}
SOUND_TYPES = {b"snd "}
PICT_TYPES = {b"PICT"}
THEME_TYPES = {b"thme", b"clut", b"pltt", b"WIND", b"scen", b"cicn", b"crsr", b"CURS", b"wctb"}


def parse_resource_fork(data):
	"""Yield (type, id, bytes) for each resource in a raw resource fork.

	The Mac OS 9 resource-fork format: a 16-byte header (data offset, map
	offset, data length, map length), then the resource data section (each
	resource prefixed by a 4-byte length), then the resource map. The map
	names each resource's type, id and offset, so we read each body from its
	offset; where a fork's map is missing or odd, we fall back to reading the
	data section in order, matching the map's type counts. Returns a list of
	(type, id, body) tuples."""
	if len(data) < 16:
		return []
	data_offset, map_offset, data_length, map_length = struct.unpack(">IIII", data[:16])

	def from_map():
		if not (0 < map_offset + 28 <= len(data) and data_offset + data_length <= len(data)):
			return []
		if map_offset + 28 > len(data):
			return []
		type_list_off, _name_list_off = struct.unpack(">HH", data[map_offset + 24:map_offset + 28])
		tl = map_offset + type_list_off
		if tl + 2 > len(data):
			return []
		items = []
		num_types = struct.unpack(">H", data[tl:tl + 2])[0] + 1
		pos = tl + 2
		for _ in range(num_types):
			if pos + 8 > len(data):
				break
			rtype = data[pos:pos + 4]
			num_res = struct.unpack(">H", data[pos + 4:pos + 6])[0] + 1
			ref_list_off = struct.unpack(">H", data[pos + 6:pos + 8])[0]
			pos += 8
			# The reference list is just `num_res` 12-byte entries (no
			# leading count): offset points straight at the first entry.
			rp = tl + ref_list_off
			for _ in range(num_res):
				if rp + 12 > len(data):
					break
				res_id = struct.unpack(">h", data[rp:rp + 2])[0]
				abs_data_off = data_offset + int.from_bytes(data[rp + 5:rp + 8], "big")
				rp += 12
				if abs_data_off + 4 > len(data):
					continue
				length = struct.unpack(">I", data[abs_data_off:abs_data_off + 4])[0]
				if length == 0 or length > 100000 or abs_data_off + 4 + length > len(data):
					continue
				items.append((rtype, res_id, data[abs_data_off + 4:abs_data_off + 4 + length]))
		return items

	def sequential():
		if not (0 < map_offset < len(data) and 0 < data_length and 0 < map_length):
			map_offset2 = struct.unpack(">I", data[-4:])[0]
			if not (0 < map_offset2 + 28 <= len(data)):
				return []
			data_offset2 = 0
			data_length2 = map_offset2
			tl = map_offset2 + 28
		else:
			data_offset2 = data_offset
			data_length2 = data_length
			tl = map_offset + 28
		if tl + 2 > len(data):
			return []
		num_types = struct.unpack(">H", data[tl:tl + 2])[0] + 1
		types = []
		pos = tl + 2
		for _ in range(num_types):
			if pos + 8 > len(data):
				break
			rtype = data[pos:pos + 4]
			num_res = struct.unpack(">H", data[pos + 4:pos + 6])[0] + 1
			types.append((rtype, num_res))
			pos += 8
		items = []
		data_pos = data_offset2
		data_end = data_offset2 + data_length2
		type_idx = 0
		res_count_in_type = 0
		while data_pos < data_end:
			if data_pos + 4 > len(data):
				break
			length = struct.unpack(">I", data[data_pos:data_pos + 4])[0]
			if length == 0 or length > 100000 or data_pos + 4 + length > len(data):
				break
			body = data[data_pos + 4:data_pos + 4 + length]
			while type_idx < len(types) and res_count_in_type >= types[type_idx][1]:
				type_idx += 1
				res_count_in_type = 0
			if type_idx < len(types):
				items.append((types[type_idx][0], res_count_in_type, body))
				res_count_in_type += 1
			data_pos += 4 + length
		return items

	items = from_map()
	return items if items else sequential()


def extract_apple_double(data):
	"""Return the resource fork from an AppleDouble (._file) stream, or None.

	AppleDouble: a 4-byte magic (0x00051607), a version, a 16-byte filler, a
	u16 entry count, then 12-byte entries (id, offset, length). The resource
	fork has id 2."""
	if len(data) < 26 or data[:4] != b"\x00\x05\x16\x07":
		return None
	num = struct.unpack(">H", data[24:26])[0]
	pos = 26
	for _ in range(num):
		if pos + 12 > len(data):
			break
		entry_id, off, length = struct.unpack(">III", data[pos:pos + 12])
		pos += 12
		if entry_id == 2:
			return data[off:off + length]
	return None


def looks_like_resource_fork(data):
	if len(data) < 16:
		return False
	data_off, map_off, data_len, map_len = struct.unpack(">IIII", data[:16])
	# Standard format: data starts at data_off, map at map_off.
	if (data_off >= 0 and 0 < map_off < len(data) and
			0 < data_len and 0 < map_len and
			data_off + data_len + map_len == len(data)):
		return True
	# Fallback: last 4 bytes are the map offset (some variants).
	map_offset = struct.unpack(">I", data[-4:])[0]
	return 0 < map_offset + 28 <= len(data)


def write_unique(out_dir, fn, blob):
	os.makedirs(out_dir, exist_ok=True)
	dest = os.path.join(out_dir, fn)
	base, ext = os.path.splitext(fn)
	n = 1
	while os.path.exists(dest):
		dest = os.path.join(out_dir, "%s_%d%s" % (base, n, ext))
		n += 1
	with open(dest, "wb") as f:
		f.write(blob)
	return dest


def bucket_for(rtype):
	if rtype in ICON_TYPES or rtype in PICT_TYPES:
		return "icons"
	if rtype in PATTERN_TYPES:
		return "patterns"
	if rtype in SOUND_TYPES:
		return "sounds"
	if rtype in THEME_TYPES:
		return "theme"
	return None


def parse_clut(body):
    """Parse a clut resource body into a list of (index, (r, g, b)) tuples."""
    if len(body) < 8:
        return []
    # ctSeed(4) + ctFlags(2) + ctSize(2) + colours
    ct_size = struct.unpack(">H", body[6:8])[0]
    colours = []
    off = 8
    for i in range(ct_size + 1):
        if off + 8 > len(body):
            break
        idx = struct.unpack(">H", body[off:off+2])[0]
        r = struct.unpack(">H", body[off+2:off+4])[0] >> 8
        g = struct.unpack(">H", body[off+4:off+6])[0] >> 8
        b = struct.unpack(">H", body[off+6:off+8])[0] >> 8
        colours.append((idx, (r, g, b)))
        off += 8
    return colours


def decode_scen_collection(data):
	"""Decode a flattened Appearance Manager collection (a 'scen' resource).

	The collection flattens to: version (0x00010000), default attributes
	(0x40000000), an item count, then one 16-byte header per item (tag, id,
	attributes, value offset), then the value pool. Each header's last word
	is the value's offset into the pool. Returns a tag -> typed value dict."""
	tags = {}
	if len(data) < 12:
		return tags
	try:
		ver, _attrs, count = struct.unpack(">III", data[:12])
	except struct.error:
		return tags
	if ver != 0x00010000 or count <= 0 or count > 1000:
		return tags
	pos = 12
	items = []
	for _ in range(count):
		if pos + 16 > len(data):
			return tags
		tag, _iid, _attrs, off = struct.unpack(">4sIII", data[pos:pos + 16])
		items.append((tag.decode("latin1"), off))
		pos += 16
	if pos + 4 > len(data):
		return tags
	total = struct.unpack(">I", data[pos:pos + 4])[0]
	pool = pos + 4
	if pool + total > len(data) + 4:
		return tags
	strings = ("name", "varn", "dpnm", "hcnm", "lgsf", "smsf", "sndt", "vfnt", "patn")
	for tag, off in items:
		p = pool + off
		if p >= len(data):
			continue
		if tag in strings:
			ln = data[p]
			tags[tag] = data[p + 1:p + 1 + ln].decode("latin1", "replace")
		elif tag in ("coll", "smoo", "snds", "user"):
			tags[tag] = bool(data[p])
		elif tag in ("epic", "vfsz"):
			if p + 2 <= len(data):
				tags[tag] = struct.unpack(">h", data[p:p + 2])[0]
		elif tag == "smos":
			if p + 2 <= len(data):
				tags[tag] = struct.unpack(">H", data[p:p + 2])[0]
		elif tag == "smsk":
			if p + 4 <= len(data):
				tags[tag] = struct.unpack(">I", data[p:p + 4])[0]
		elif tag in ("sbar", "sbth"):
			if p + 2 <= len(data):
				tags[tag] = struct.unpack(">H", data[p:p + 2])[0]
		elif tag == "hcol":
			if p + 6 <= len(data):
				r, g, b = struct.unpack(">HHH", data[p:p + 6])
				tags[tag] = (r, g, b)
	return tags


WCTB_PARTS = {
	0: "window-content", 1: "window-frame", 2: "window-text", 3: "window-hilite",
	4: "window-titlebar", 5: "window-hilite-light", 6: "window-hilite-dark",
	7: "window-titlebar-light", 8: "window-titlebar-dark", 9: "window-dialog-light",
	10: "window-dialog-dark", 11: "window-tinge-light", 12: "window-tinge-dark",
}


def decode_wctb(data):
	"""Decode a window colour table ('wctb') to {part-name: "#rrggbb"}.

	Format: 6 unused bytes, then a u16 count-minus-one, then (count) records
	of {part id (u16), r (u16), g (u16), b (u16)}."""
	if len(data) < 8:
		return {}
	count = struct.unpack(">H", data[6:8])[0] + 1
	out = {}
	pos = 8
	for _ in range(count):
		if pos + 8 > len(data):
			break
		part, r, g, b = struct.unpack(">HHHH", data[pos:pos + 8])
		name = WCTB_PARTS.get(part, "window-part-%d" % part)
		out[name] = "%02x%02x%02x" % (r >> 8, g >> 8, b >> 8)
		pos += 8
	return out


def process_resource_fork(data, out, counts, cluts, icons, assets, scen_tags, apple_menu, wctb, raw_icons):
	for rtype, res_id, body in parse_resource_fork(data):
		bucket = bucket_for(rtype)
		if not bucket:
			continue
		if bucket != "sounds":
			# Sounds are written as decoded WAVs by write_assets, never as raw .bin.
			write_unique(os.path.join(out, bucket), "%s_%d.bin" % (rtype.decode("latin1"), res_id), body)
		counts[bucket] += 1
		if rtype == b"clut":
			cols = parse_clut(body)
			if cols:
				cluts.append((res_id, cols))
		if rtype == b"scen":
			for tag, value in decode_scen_collection(body).items():
				if tag not in scen_tags:
					scen_tags[tag] = value
		if rtype in (b"icl8", b"icl4", b"ICN#"):
			icons.setdefault(rtype, []).append(body)
		if rtype in (b"ics8", b"ics4", b"ics#") and res_id == -16386:
			apple_menu.setdefault(rtype, []).append(body)
		if rtype == b"wctb" and res_id == 0 and not wctb:
			wctb.update(decode_wctb(body))
		if rtype in (b"cicn", b"crsr", b"CURS"):
			raw_icons.setdefault(rtype, {})[res_id] = body
		if rtype == b"PICT":
			assets.setdefault("pict", []).append(body)
		if rtype in (b"PAT ", b"PAT#"):
			assets.setdefault("pat", []).append(body)
		if rtype == b"snd ":
			assets.setdefault("snd", []).append(body)


def write_apple_menu(apple_menu, cluts, out):
	"""Write apple-menu.png from the System file's 16x16 menu-bar Apple."""
	color = None
	mask = None
	if apple_menu.get(b"ics8"):
		color = apple_menu[b"ics8"][0]
	elif apple_menu.get(b"ics4"):
		color = apple_menu[b"ics4"][0]
	if not color:
		return
	if apple_menu.get(b"ics#"):
		_, mask_rows = macicons.decode_icn1(apple_menu[b"ics#"][0], size=16)
		mask = mask_rows
	# The 8-bit icons index the System's default 256-entry colour table:
	# use the largest clut in the file.
	palette = None
	for _res_id, cols in cluts:
		if cols and (palette is None or len(cols) > len(palette)):
			palette = cols
	rows = macicons.decode_icl8(color, palette, size=16) if len(color) == 256 else 		macicons.decode_icl4(color, palette, size=16) if len(color) == 128 else None
	if rows is None:
		return
	if mask:
		rows = macicons.combine_icon(rows, mask)
	pngwrite.write_png(os.path.join(out, "apple-menu.png"), 16, 16, rows)


def write_icon_dump(raw_icons, cluts, out):
	"""Decode cicn/crsr/CURS resources for visual checking (--dump-icons)."""
	# Indexed icons pick their palette by depth: 4-bit uses the 16-entry
	# system table, 8-bit the 256-entry table.
	cluts_by_size = {}
	for _rid, cols in cluts:
		cluts_by_size.setdefault(len(cols), cols)
	lines = []
	for rtype in (b"cicn", b"crsr", b"CURS"):
		for res_id, body in sorted(raw_icons.get(rtype, {}).items()):
			tag = rtype.decode("latin1")
			try:
				if rtype == b"cicn":
					rows = macicons.decode_cicn(body, cluts=cluts_by_size)
					sub = "icons"
					name = "cicn_%d.png" % res_id
				elif rtype == b"crsr":
					rows, hot = macicons.decode_crsr(body, cluts=cluts_by_size)
					sub = "cursors"
					name = "crsr_%d.png" % res_id
					lines.append("%s %d hotspot=%d,%d" % (tag, res_id, hot[0], hot[1]))
				else:
					continue
				pngwrite.write_png(os.path.join(out, sub, name), len(rows[0]), len(rows), rows)
				lines.append("%s %d -> %s/%s" % (tag, res_id, sub, name))
			except (ValueError, IndexError, struct.error) as e:
				lines.append("%s %d skipped: %s" % (tag, res_id, e))
	with open(os.path.join(out, "icons-dump.txt"), "w") as f:
		f.write("\n".join(lines) + "\n")
	print("Wrote icons-dump.txt (%d resources decoded)" % len(lines))


# Pointer roles. The System file supplies the busy, resize, move, hand and
# crosshair pointers; a theme file supplies the arrow family (its colour
# 'crsr' 128-131). Every name listed is written as the same cursor file.
SYSTEM_POINTERS = (
	("CURS", range(-20721, -20714), ("watch", "wait")),        # busy: wristwatch
	("CURS", range(-20708, -20700), ("progress",)),           # busy: spinning wheel
	("CURS", (-20852,), ("left_side", "w-resize")),
	("CURS", (-20851,), ("right_side", "e-resize")),
	("CURS", (-20850,), ("fleur", "move")),
	("CURS", (3,), ("crosshair", "cross")),
	("CURS", (-5760,), ("not-allowed", "no-drop")),
	("crsr", (-20877,), ("pointer", "hand2")),
	("crsr", (-20876,), ("grabbing",)),
)
THEME_POINTERS = (
	(128, ("default", "left_ptr", "arrow")),
	(129, ("copy", "dnd-copy")),
	(130, ("alias", "link", "dnd-link")),
	(131, ("context-menu",)),
)
POINTER_SIZES = (16, 32, 48)  # 1x, 2x, 3x; scaled nearest-neighbour from 16 px
POINTER_FRAME_MS = 100


def write_xcursor(path, frames, hot, delay, sizes):
	"""Write an Xcursor file: each frame at each nominal size, in order."""
	chunks = []
	toc = []
	pos = 16 + 12 * len(sizes) * len(frames)
	for size in sizes:
		k = size // 16
		for rows in frames:
			pixels = bytearray()
			for y in range(size):
				for x in range(size):
					r, g, b, a = rows[y // k][x // k]
					pixels += struct.pack("<I", (a << 24) | (r << 16) | (g << 8) | b if a else 0)
			header = struct.pack("<IIIIIIIII", 36, 0xFFFD0002, size, 1, size, size,
				hot[0] * k, hot[1] * k, delay)
			toc.append((0xFFFD0002, size, pos))
			chunks.append(header + bytes(pixels))
			pos += len(header) + len(pixels)
	out = b"Xcur" + struct.pack("<III", 16, 0x10000, len(toc))
	for typ, subtype, offset in toc:
		out += struct.pack("<III", typ, subtype, offset)
	with open(path, "wb") as f:
		f.write(out + b"".join(chunks))


def decode_pointer(kind, body, cluts):
	if kind == "CURS":
		return macicons.decode_curs(body)
	return macicons.decode_crsr(body, cluts=cluts)


def write_pointers(theme_raw, theme_cluts, system_raw, system_cluts, out):
	"""Write cursors/<name> for every pointer the two sources supply, and an
	index.theme that inherits everything else from ZacOS9. Returns False when
	neither source has a pointer (the bundle then keeps ZacOS9's cursors).

	The theme directory's name is the Xcursor theme name the compositor uses."""
	entries = [(system_raw, system_cluts, kind, ids, names) for kind, ids, names in SYSTEM_POINTERS]
	entries += [(theme_raw, theme_cluts, "crsr", (rid,), names) for rid, names in THEME_POINTERS]
	cursor_dir = os.path.join(out, "cursors")
	written = 0
	for raw, cluts, kind, ids, names in entries:
		bodies = raw.get(kind, {})
		if not all(i in bodies for i in ids):
			continue
		frames = []
		hot = None
		try:
			for i in ids:
				rows, spot = decode_pointer(kind, bodies[i], cluts)
				frames.append(rows)
				hot = hot or spot
		except (ValueError, IndexError, struct.error):
			continue
		os.makedirs(cursor_dir, exist_ok=True)
		delay = POINTER_FRAME_MS if len(frames) > 1 else 0
		for name in names:
			write_xcursor(os.path.join(cursor_dir, name), frames, hot, delay, POINTER_SIZES)
		written += 1
	if not written:
		return False
	with open(os.path.join(out, "index.theme"), "w") as f:
		f.write("[Icon Theme]\nName=%s\nInherits=ZacOS9\n" % os.path.basename(out.rstrip("/")))
	return True


def pointer_resources(path):
	"""The pointer resources and colour tables of a System or theme file,
	as ({type: {id: body}}, {colour count: colours})."""
	with open(path, "rb") as f:
		raw = f.read()
	fork = extract_apple_double(raw) if path.endswith("._") or raw[:4] == b"\x00\x05\x16\x07" else raw
	items = parse_resource_fork(fork)
	res = {}
	cluts = {}
	for rtype, rid, body in items:
		res.setdefault(rtype.decode("latin1"), {})[rid] = body
		if rtype == b"clut":
			cols = parse_clut(body)
			cluts.setdefault(len(cols), cols)
	return res, cluts


def write_assets(assets, out):
	"""Decode PICT/PAT/snd resources into wallpapers, patterns and sounds."""
	# Wallpapers from PICT.
	for i, body in enumerate(assets.get("pict", [])):
		try:
			w, h, rows = macpict.decode_pict(body)
			pngwrite.write_png(os.path.join(out, "wallpapers", "picture-%d.png" % (i + 1)), w, h, rows)
		except (ValueError, IndexError, struct.error):
			continue
	# Desktop pattern tiles from PAT.
	for i, body in enumerate(assets.get("pat", [])):
		try:
			rows = macpat.decode_pat(body)
			pngwrite.write_png(os.path.join(out, "patterns", "pattern-%d.png" % (i + 1)), 8, 8, rows)
		except (ValueError, IndexError):
			continue
	# Alert sounds from snd.
	for i, body in enumerate(assets.get("snd", [])):
		try:
			macsnd.snd_to_wav(body, os.path.join(out, "sounds", "sound-%d.wav" % (i + 1)))
		except (ValueError, struct.error):
			continue


def write_theme_art(icons, cluts, out):
	"""Write menu-icon.png and boot/*.png from the theme's icon resources."""
	color = None
	mask = None
	# Prefer icl8 (32x32 colour); fall back to icl4.
	if icons.get(b"icl8"):
		color = icons[b"icl8"][0]
	elif icons.get(b"icl4"):
		color = icons[b"icl4"][0]
	if not color:
		return
	if icons.get(b"ICN#"):
		mask = icons[b"ICN#"][0]

	# Pick a clut: the first gray ramp, else the first available.
	palette = None
	for res_id, cols in cluts:
		if cols and cols[0][1][0] == cols[0][1][1] == cols[0][1][2]:
			palette = cols
			break
	if palette is None and cluts:
		palette = cluts[0][1]

	rows = macicons.decode_icl8(color, palette) if len(color) == 1024 else \
		macicons.decode_icl4(color, palette) if len(color) == 512 else None
	if rows is None:
		return
	if mask:
		_, mask_rows = macicons.decode_icn1(mask)
		rows = macicons.combine_icon(rows, mask_rows)

	# Menu icon: 20 x 15.
	pngwrite.write_png(os.path.join(out, "menu-icon.png"), 20, 15,
		pngwrite.fit_scale(rows, 32, 32, 20, 15))
	# Boot art: 32 * scale, scale 1..6.
	for scale in range(1, 7):
		size = 32 * scale
		scaled = macicons.decode_icl8(color, palette) if len(color) == 1024 else \
			macicons.decode_icl4(color, palette)
		if mask:
			scaled = macicons.combine_icon(scaled, mask_rows)
		boot = pngwrite.nearest_scale(scaled, 32, 32, size, size)
		pngwrite.write_png(os.path.join(out, "boot", "happy-zac-%dx.png" % scale),
			size, size, boot)
		pngwrite.write_png(os.path.join(out, "boot", "zacos-logo-%dx.png" % scale),
			size, size, boot)


ARCHIVE_EXTS = {".sit", ".hqx", ".sea", ".cpt"}


def expand_archives(src):
	"""Expand StuffIt/BinHex archives under `src` with The Unarchiver.

	`unar -forks hidden` writes each resource fork as a "._Name" AppleDouble
	file beside its data fork, which the walker below reads. Returns the list
	of temp directories holding the expanded files (the caller does not need
	to clean them up: they live in the system temp area)."""
	extras = []
	for root, _dirs, files in os.walk(src):
		if "/.finf" in root or "/.rsrc" in root:
			continue
		for fn in files:
			if os.path.splitext(fn)[1].lower() not in ARCHIVE_EXTS:
				continue
			full = os.path.join(root, fn)
			dest = tempfile.mkdtemp(prefix="zacos9-theme-")
			r = subprocess.run(["unar", "-forks", "hidden", "-o", dest, full],
				capture_output=True)
			if r.returncode == 0:
				extras.append(dest)
			else:
				print("unar couldn't expand %s: %s" % (full, r.stderr.decode("utf-8", "replace").strip()), file=sys.stderr)
	return extras


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--source", help="Mac OS 9 tree (defaults to ~/Classic)")
	parser.add_argument("--file", help="A single theme file (resource fork or AppleDouble)")
	parser.add_argument("--output", required=True, help="Destination theme bundle directory")
	parser.add_argument("--name", help="Theme name (defaults to the source folder's name)")
	parser.add_argument("--accent", help="Accent colour id (lavender, graphite, gold, bronze, ivy, ocean, rose, plum)")
	parser.add_argument("--highlight", help="Text highlight colour as RRGGBB")
	parser.add_argument("--pattern", help="Desktop pattern id")
	parser.add_argument("--list-cluts", action="store_true", help="Print colour tables from clut resources and exit")
	parser.add_argument("--auto-colours", action="store_true", help="Heuristically pick chrome colours from the clut resources")
	parser.add_argument("--system", help="The System file, for its busy, resize, move and hand pointers")
	parser.add_argument("--dump-icons", action="store_true", help="Decode cicn/crsr/CURS resources to PNG and a text dump")
	args = parser.parse_args()

	out = os.path.abspath(args.output)
	for sub in ("icons", "patterns", "sounds", "theme", "boot", "wallpapers", "cursors"):
		os.makedirs(os.path.join(out, sub), exist_ok=True)

	counts = {"icons": 0, "patterns": 0, "sounds": 0, "theme": 0}
	cluts = []
	icons = {}
	assets = {}
	scen_tags = {}
	apple_menu = {}
	wctb = {}
	raw_icons = {}

	if args.file:
		# A single theme file the user copied out of Mac OS 9.
		filepath = os.path.abspath(args.file)
		if not os.path.isfile(filepath):
			print("file not found: " + filepath, file=sys.stderr)
			return 2
		base = os.path.splitext(os.path.basename(filepath))[0]
		name = args.name or base.replace(" copy", "").replace(" Copy", "").strip() or "Mac OS 9 Theme"
		src = filepath
		with open(filepath, "rb") as f:
			raw = f.read()
		# AppleDouble files carry the resource fork; a raw resource fork is
		# used as-is.
		if filepath.endswith("._") or raw[:4] == b"\x00\x05\x16\x07":
			fork = extract_apple_double(raw)
		else:
			fork = raw
		if fork and looks_like_resource_fork(fork):
			process_resource_fork(fork, out, counts, cluts, icons, assets, scen_tags, apple_menu, wctb, raw_icons)
	else:
		src = args.source
		if not src:
			src = os.path.expanduser("~/Classic")
		if not os.path.isdir(src):
			print("source is not a directory: " + src, file=sys.stderr)
			print("Hint: set up Old Dog first (put your Mac OS 9 install in ~/Classic).", file=sys.stderr)
			return 2
		src = os.path.abspath(src)
		name = args.name or os.path.basename(src.rstrip(os.sep)) or "Mac OS 9 Theme"

		def walk_tree(base):
			for root, _dirs, files in os.walk(base):
				for fn in files:
					full = os.path.join(root, fn)
					ext = os.path.splitext(fn)[1].lower()
					# Disk images and ROMs are far larger than any theme asset; skip them.
					if os.path.getsize(full) > 64 * 1024 * 1024:
						continue
					with open(full, "rb") as f:
						raw = f.read()

					# AppleDouble files carry the resource fork.
					if fn.startswith("._"):
						fork = extract_apple_double(raw)
						if fork:
							process_resource_fork(fork, out, counts, cluts, icons, assets, scen_tags, apple_menu, wctb, raw_icons)
							continue
						# Not AppleDouble after all: fall through.

					# A raw resource fork (not an AppleDouble) when the tail maps back.
					if looks_like_resource_fork(raw) and not fn.startswith("._"):
						process_resource_fork(raw, out, counts, cluts, icons, assets, scen_tags, apple_menu, wctb, raw_icons)
						continue

					# Loose asset files.
					if ext in ASSET_EXTS:
						if ext in (".icns", ".png", ".jpg", ".jpeg"):
							bucket = "icons"
						elif ext == ".pat":
							bucket = "patterns"
						else:
							bucket = "sounds"
						write_unique(os.path.join(out, bucket), fn, raw)
						counts[bucket] += 1

			# Netatalk / AFP AppleDouble convention: resource forks live in a
			# `.rsrc` directory, finder info in `.finf`, next to the data forks.
			for root, _dirs, files in os.walk(base):
				if os.path.basename(root) != ".rsrc":
					continue
				for fn in files:
					full = os.path.join(root, fn)
					with open(full, "rb") as f:
						raw = f.read()
					if looks_like_resource_fork(raw):
						process_resource_fork(raw, out, counts, cluts, icons, assets, scen_tags, apple_menu, wctb, raw_icons)

		for s in [src] + expand_archives(src):
			walk_tree(s)

	# Turn the theme's icon into the menu icon and boot art.
	write_theme_art(icons, cluts, out)
	# The System file's Apple menu icon (ics8/ics4 id -16386), for themes
	# the user builds from their own Mac OS 9 install.
	write_apple_menu(apple_menu, cluts, out)
	# Pointers: the arrow family from the theme file, the rest from the System file.
	theme_raw = {k.decode("latin1"): v for k, v in raw_icons.items()}
	theme_cluts = {}
	for _rid, cols in cluts:
		theme_cluts.setdefault(len(cols), cols)
	system_raw, system_cluts = pointer_resources(args.system) if args.system else ({}, {})
	write_pointers(theme_raw, theme_cluts, system_raw, system_cluts, out)
	if args.dump_icons:
		write_icon_dump(raw_icons, cluts, out)
	# Decode wallpapers, patterns and sounds.
	write_assets(assets, out)

	if args.list_cluts:
		for res_id, cols in cluts:
			print("clut %d (%d colours):" % (res_id, len(cols)))
			for idx, (r, g, b) in cols:
				print("  %3d: #%02x%02x%02x" % (idx, r, g, b))
		return 0

	# Auto-colour heuristic: pick the first small (8-colour) clut whose
	# first colour is a grey for the window chrome, and the first non-grey
	# clut for the accent.
	auto_accent = None
	auto_highlight = None
	auto_highlight_b = -1
	auto_title_base = None
	auto_title_stripe_light = None
	auto_title_stripe_dark = None
	auto_frame_light = None
	auto_frame_dark = None
	auto_frame_border = None
	auto_frame_face = None
	if args.auto_colours:
		for res_id, cols in cluts:
			if len(cols) != 8:
				continue
			r0, g0, b0 = cols[0][1]
			if r0 == g0 == b0:
				# Gray ramp: Platinum window chrome.
				if auto_title_base is None:
					auto_title_base = "%02x%02x%02x" % (r0, g0, b0)
					auto_title_stripe_dark = "%02x%02x%02x" % cols[3][1] if len(cols) > 3 else None
					auto_frame_light = "ffffff"
					auto_frame_dark = "%02x%02x%02x" % cols[2][1] if len(cols) > 2 else None
					auto_frame_border = "%02x%02x%02x" % cols[7][1] if len(cols) > 7 else "000000"
					auto_frame_face = "%02x%02x%02x" % cols[1][1] if len(cols) > 1 else None
			else:
				if auto_highlight is None or (cols[0][1][2] > auto_highlight_b and cols[0][1][2] > cols[0][1][0]):
					# Prefer blue-ish cluts (the default Platinum accent).
					mid = cols[len(cols) // 2][1]
					auto_highlight = "%02x%02x%02x" % mid
					auto_highlight_b = cols[0][1][2]

	scen_name = scen_tags.get("name")
	scen_highlight = None
	if "hcol" in scen_tags:
		r, g, b = scen_tags["hcol"]
		scen_highlight = "%02x%02x%02x" % (r >> 8, g >> 8, b >> 8)

	with open(os.path.join(out, "theme.conf"), "w") as f:
		f.write("name=%s\n" % (name or scen_name or "Mac OS 9 Theme"))
		f.write("source=%s\n" % src)
		accent = args.accent or auto_accent
		highlight = args.highlight or scen_highlight or auto_highlight
		pattern = args.pattern
		title_base = auto_title_base
		title_stripe_light = auto_title_stripe_light
		title_stripe_dark = auto_title_stripe_dark
		frame_light = auto_frame_light
		frame_dark = auto_frame_dark
		frame_border = auto_frame_border
		frame_face = auto_frame_face
		if scen_tags.get("varn"):
			f.write("variant=%s\n" % scen_tags["varn"])
		if scen_tags.get("lgsf"):
			f.write("system-font=%s\n" % scen_tags["lgsf"])
		if scen_tags.get("smsf"):
			f.write("small-system-font=%s\n" % scen_tags["smsf"])
		if scen_tags.get("vfnt"):
			f.write("views-font=%s\n" % scen_tags["vfnt"])
		if "vfsz" in scen_tags:
			f.write("views-font-size=%d\n" % scen_tags["vfsz"])
		if "coll" in scen_tags:
			f.write("double-click-collapse=%d\n" % (1 if scen_tags["coll"] else 0))
		if "smoo" in scen_tags:
			f.write("smooth-fonts=%d\n" % (1 if scen_tags["smoo"] else 0))
		if "smos" in scen_tags:
			f.write("smooth-font-min-size=%d\n" % scen_tags["smos"])
		if scen_tags.get("dpnm"):
			f.write("desktop-picture=%s\n" % scen_tags["dpnm"])
		if scen_tags.get("patn"):
			f.write("pattern-name=%s\n" % scen_tags["patn"])
		if scen_tags.get("hcnm"):
			f.write("highlight-name=%s\n" % scen_tags["hcnm"])
		if "sbar" in scen_tags:
			f.write("scroll-bar-arrows=%s\n" %
				("double-lower-right" if scen_tags["sbar"] == 1 else "single"))
		if "sbth" in scen_tags:
			f.write("scroll-bar-thumb=%s\n" %
				("proportional" if scen_tags["sbth"] == 1 else "normal"))
		for key in ("window-frame", "window-text", "window-titlebar-light",
				"window-titlebar-dark", "window-dialog-light", "window-dialog-dark"):
			if key in wctb:
				f.write("%s=%s\n" % (key, wctb[key]))
		if accent:
			f.write("accent=%s\n" % accent)
		if highlight:
			f.write("highlight=%s\n" % highlight)
		if pattern:
			f.write("pattern=%s\n" % pattern)
		if title_base:
			f.write("title-base=%s\n" % title_base)
		if title_stripe_light:
			f.write("title-stripe-light=%s\n" % title_stripe_light)
		if title_stripe_dark:
			f.write("title-stripe-dark=%s\n" % title_stripe_dark)
		if frame_light:
			f.write("frame-light=%s\n" % frame_light)
		if frame_dark:
			f.write("frame-dark=%s\n" % frame_dark)
		if frame_border:
			f.write("frame-border=%s\n" % frame_border)
		if frame_face:
			f.write("frame-face=%s\n" % frame_face)

	print("Created theme bundle: " + out)
	print("  icons:    %d" % counts["icons"])
	print("  patterns: %d" % counts["patterns"])
	print("  sounds:   %d" % counts["sounds"])
	print("  theme:    %d" % counts["theme"])
	print("Note: Appearance theme (thme/clut/pltt) resources are saved raw under")
	print("theme/ for analysis; pass --accent/--highlight/--pattern to set")
	print("window chrome colours. Resource-fork assets must be extracted first")
	print("(e.g. with `macunpack` from the macutils package).")
	return 0


if __name__ == "__main__":
	sys.exit(main())
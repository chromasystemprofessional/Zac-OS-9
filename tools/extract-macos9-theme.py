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
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile

# Find the decoder modules beside this script (source tree or installed).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import macfont
import macicons
import macfont
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
FONT_TYPES = {b"sfnt", b"FOND", b"nfnt"}
CURSOR_TYPES = {b"CURS", b"crsr"}
PICT_TYPES = {b"PICT"}
THEME_TYPES = {b"thme", b"clut", b"pltt", b"WIND", b"scen", b"pxm#", b"layo", b"plut",
	b"cicn", b"crsr", b"CURS", b"wctb"}

# Mac OS 9 pointer shapes that become the desktop's cursors, as
# (bundle name, file, resource type, id). 16x16 1-bit with a hotspot,
# verified against a running Mac OS 9 desktop. The arrow pointer lives
# only in the Mac ROM, so it is captured from a boot screenshot instead
# (like the Happy Mac); everything else decodes here.
CURSOR_MAP = [
    ("xterm", "System", b"CURS", 1),
    ("crosshair", "System", b"CURS", 2),
    ("move", "System", b"CURS", 3),
    ("watch", "System", b"CURS", 4),
    ("hand1", "Finder", b"CURS", 6603),
    ("hand2", "Finder", b"CURS", 6604),
]
CURSOR_IDS = frozenset((rtype, rid) for _kind, _src, rtype, rid in CURSOR_MAP)

# Mac OS 9 icon families that become the desktop's icons, as
# (bundle kind name, colour type, id). Decoded at 32 and 16 px from the
# System file (and the Finder file, for alerts) in both modes; every entry
# was verified against a running Mac OS 9 desktop (docs/themes.md).
THEME_ICON_MAP = [
    ("folder", b"icl8", -3999),
    ("system-folder", b"icl8", -3983),
    ("trash-empty", b"icl8", -3993),
    ("trash-full", b"icl8", -3984),
    ("document", b"icl8", -4000),
    # Apple's generic hard disk. Finder family 400 is the desktop
    # *printer*; the Finder's own desktop drive is family 29010, used
    # when the theme supplies no -3995 art of its own.
    ("disk", b"icl8", -3995),
    ("network-disk", b"icl8", -3972),
    ("disk-image", b"icl8", -3998),
    ("caution", b"icl8", 7202),
]
# Mask resource for each colour icon type.
ICON_MASKS = {b"icl8": b"ICN#", b"icl4": b"ICN#", b"ics8": b"ics#", b"ics4": b"ics#"}

# Window chrome lined up per theme from the catalog sheets (verified
# renders): ppat ids for the title-bar tiles, pxm# ids for the buttons.
# Keys are bundle names as derived from the file (Qt's completeBaseName
# strips the dotted tail, so "Aqua v2.0" arrives as "Aqua v2"). Only
# listed themes get art; everything else keeps flat chrome colours.
# Bar middles stretch edge to edge (caps are optional and currently
# unmapped); buttons are 16-bit pxm# layers, layer 1 normal, layer 0 pressed.
THEME_CHROME = {
	"Aqua v2": {
		"bar-middle": 3102,
		"bar-middle-inactive": 20609,
		"button-close": 200, "button-close-pressed": 200,
		"button-zoom": 210, "button-zoom-pressed": 210,
		"button-grow": 220, "button-grow-pressed": 220,
	},
	"Aqua  v2.0": {
		"bar-middle": 3102,
		"bar-middle-inactive": 20609,
		"button-close": 200, "button-close-pressed": 200,
		"button-zoom": 210, "button-zoom-pressed": 210,
		"button-grow": 220, "button-grow-pressed": 220,
	},
}

THEME_ICON_IDS = frozenset(
    [(rtype, rid) for _kind, rtype, rid in THEME_ICON_MAP] +
    [(ICON_MASKS[rtype], rid) for _kind, rtype, rid in THEME_ICON_MAP] +
    [(b"icns", rid) for _kind, _rtype, rid in THEME_ICON_MAP] +
    [(b"ics8", rid) for _kind, _rtype, rid in THEME_ICON_MAP] +
    [(b"ics4", rid) for _kind, _rtype, rid in THEME_ICON_MAP] +
    [(b"ics#", rid) for _kind, _rtype, rid in THEME_ICON_MAP] +
    # The Finder's desktop drive family (hard disk 29010, floppy 29011,
    # CD 29012, locked 29013), not in THEME_ICON_MAP.
    [(t, 29010) for t in (b"icl8", b"icl4", b"icns", b"ics8", b"ics4", b"ics#", b"ICN#")]
)


def decode_standard_icon(theme_icons, palette, rid, size):
	"""One standard icon at `size` (16 or 32) from whatever the file holds.

	A Mac OS 9 file may carry an icon suite (icns) or any of the colour
	families (icl8/icl4, ics8/ics4) for the same id; the maps name one
	type, but the art can be in another. Same order everywhere: suite,
	then the colour family, then 4-bit, each with its 1-bit mask."""
	if (b"icns", rid) in theme_icons:
		try:
			return macicons.decode_icns(theme_icons[(b"icns", rid)][0], palette, size=size)
		except (ValueError, IndexError, struct.error):
			pass
	if size >= 32:
		families = ((b"icl8", 1024, macicons.decode_icl8), (b"icl4", 512, macicons.decode_icl4))
	else:
		families = ((b"ics8", 256, macicons.decode_icl8), (b"ics4", 128, macicons.decode_icl4))
	for rtype, expect, decode in families:
		bodies = theme_icons.get((rtype, rid))
		if not bodies or len(bodies[0]) != expect:
			continue
		try:
			rows = decode(bodies[0], palette, size=size)
		except (ValueError, IndexError):
			continue
		mask = theme_icons.get((ICON_MASKS[rtype], rid))
		if mask:
			try:
				_, mask_rows = macicons.decode_icn1(mask[0], size=size)
				rows = macicons.combine_icon(rows, mask_rows)
			except (ValueError, IndexError):
				pass
		return rows
	return None


def decode_theme_icon(theme_icons, palette, color_type, rid, size, expect, prefer_icns=True):
	"""Decode one mapped icon family to rows of RGBA, or None."""
	if prefer_icns:
		icns_bodies = theme_icons.get((b"icns", rid))
		if icns_bodies:
			try:
				return macicons.decode_icns(icns_bodies[0], palette, size=size)
			except (ValueError, IndexError, struct.error):
				pass
	bodies = theme_icons.get((color_type, rid))
	if not bodies or len(bodies[0]) != expect:
		return None
	try:
		if len(bodies[0]) in (1024, 256):
			rows = macicons.decode_icl8(bodies[0], palette, size=size)
		else:
			rows = macicons.decode_icl4(bodies[0], palette, size=size)
	except (ValueError, IndexError):
		return None
	mask = theme_icons.get((ICON_MASKS[color_type], rid))
	if mask:
		try:
			_, mask_rows = macicons.decode_icn1(mask[0], size=size)
			rows = macicons.combine_icon(rows, mask_rows)
		except (ValueError, IndexError):
			return None
	return rows


def write_file_icon(file_icon, cluts, theme_cluts, out):
	"""Decode the theme file's own Finder icon (family 1024, the mini
	window art Mac OS 9 shows for the file) into the bundle's
	file-icon.png. The Sniffer stamps it onto the dropped file so the
	theme sits in Theme Files wearing its own art. Missing silently
	when the file carries none (Aqua, Liquid); the generic document
	icon stays."""
	sys_palette = macicons.resolve_palette(cluts[len(theme_cluts):] or cluts)
	icns_bodies = file_icon.get(b"icns")
	if icns_bodies:
		try:
			rows = macicons.decode_icns(icns_bodies[0], sys_palette, size=32)
			pngwrite.write_png(os.path.join(out, "file-icon.png"), 32, 32, rows)
			return rows
		except (ValueError, IndexError, struct.error):
			pass
	bodies = file_icon.get(b"icl8")
	if not bodies or len(bodies[0]) != 1024:
		return None
	try:
		rows = macicons.decode_icl8(bodies[0], sys_palette, size=32)
	except (ValueError, IndexError):
		return None
	mask = file_icon.get(b"ICN#")
	if mask:
		try:
			_, mask_rows = macicons.decode_icn1(mask[0], size=32)
			rows = macicons.combine_icon(rows, mask_rows)
		except (ValueError, IndexError):
			return None
	pngwrite.write_png(os.path.join(out, "file-icon.png"), 32, 32, rows)
	return rows


def write_theme_icons(theme_icons, cluts, out, theme_keys=frozenset(), theme_cluts=()):
	"""Decode the mapped Mac icon families into the bundle's icons/.

	All icon art — a Kaleidoscope scheme's own as well as the System
	and Finder kinds — decodes against the System palette (calibrated):
	Mac icon families are authored against the palette they display
	through, and a scheme file's own colour tables describe its window
	chrome, not its icons (using one dyed Aqua's folder brown).
	Without a System file the theme's own tables stand in, uncalibrated.
	"""
	sys_palette = macicons.resolve_palette(cluts[len(theme_cluts):] or cluts)
	icons_dir = os.path.join(out, "icons")
	# A rebuild must not keep an old extractor's icon when this one cannot
	# find that kind: the desktop would show the wrong art forever.
	for kind, _rtype, _rid in THEME_ICON_MAP:
		for size in (16, 32):
			stale = os.path.join(icons_dir, "%s-%d.png" % (kind, size))
			try:
				os.remove(stale)
			except FileNotFoundError:
				pass
	for kind, _rtype, rid in THEME_ICON_MAP:
		use = rid
		if kind == "disk":
			# A theme's own hard disk art (family -3995) wins. Without it,
			# use the Finder's desktop drive family 29010: the 3D drive
			# Old Dog shows on the desktop, not the desktop printer (400)
			# or the flat pop-up icon (-3995).
			theme_disk = any((t, rid) in theme_keys for t in
				(b"icns", b"icl8", b"icl4", b"ics8", b"ics4", b"ics#"))
			finder_disk = any((t, 29010) in theme_icons for t in
				(b"icns", b"icl8", b"icl4", b"ics8", b"ics4", b"ics#"))
			if not theme_disk and finder_disk:
				use = 29010
		big = decode_standard_icon(theme_icons, sys_palette, use, 32)
		if big is None:
			continue
		pngwrite.write_png(os.path.join(icons_dir, kind + "-32.png"), 32, 32, big)
		small = decode_standard_icon(theme_icons, sys_palette, use, 16)
		if small is None:
			small = pngwrite.nearest_scale(big, 32, 32, 16, 16)
		pngwrite.write_png(os.path.join(icons_dir, kind + "-16.png"), 16, 16, small)


def parse_resource_fork(data):
	"""Yield (type, id, bytes) for each resource in a raw resource fork.

	The Mac OS 9 resource-fork format: a 16-byte header (data offset, map
	offset, data length, map length), then the resource data section (each
	resource prefixed by a 4-byte length), then the resource map. The map
	names each resource's type, id and offset, so we read each body from its
	offset; where a fork's map is missing or odd, we fall back to reading the
	data section in order, matching the map's type counts. Returns a list of
	(type, id, body) tuples."""
	MAX_MAP_TYPES = 4096
	MAX_MAP_RESOURCES = 20000
	MAX_MAP_BYTES = 64 * 1024 * 1024
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
		total = 0
		num_types = struct.unpack(">H", data[tl:tl + 2])[0] + 1
		# A file that is not a resource fork (an AppleDouble header, say)
		# can still satisfy the offsets above and then name tens of
		# thousands of types whose reference list reads the same bytes.
		# Real forks hold far fewer: stop before copying the same body
		# until the memory runs out.
		if num_types > MAX_MAP_TYPES:
			return []
		pos = tl + 2
		for _ in range(num_types):
			if pos + 8 > len(data):
				break
			rtype = data[pos:pos + 4]
			num_res = struct.unpack(">H", data[pos + 4:pos + 6])[0] + 1
			ref_list_off = struct.unpack(">H", data[pos + 6:pos + 8])[0]
			pos += 8
			# The reference list is just `num_res` 12-byte entries (no
			# leading count): offset points straight at the first entry,
			# and it must lie inside the map section.
			rp = tl + ref_list_off
			if num_res > MAX_MAP_RESOURCES or rp + 12 * num_res > map_offset + map_length:
				continue
			for _ in range(num_res):
				if rp + 12 > len(data) or len(items) >= MAX_MAP_RESOURCES:
					break
				res_id = struct.unpack(">h", data[rp:rp + 2])[0]
				abs_data_off = data_offset + int.from_bytes(data[rp + 5:rp + 8], "big")
				rp += 12
				if abs_data_off + 4 > len(data):
					continue
				length = struct.unpack(">I", data[abs_data_off:abs_data_off + 4])[0]
				if length == 0 or length > 100000 or abs_data_off + 4 + length > len(data):
					continue
				if total + length > MAX_MAP_BYTES:
					return items
				items.append((rtype, res_id, data[abs_data_off + 4:abs_data_off + 4 + length]))
				total += length
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
	"""Write blob to out_dir/fn, uniquifying the name when taken.

	Re-importing the same theme reuses the file when the bytes match, so
	repeated imports do not pile up _1, _2 duplicates."""
	os.makedirs(out_dir, exist_ok=True)
	dest = os.path.join(out_dir, fn)
	base, ext = os.path.splitext(fn)
	n = 1
	while os.path.exists(dest):
		try:
			with open(dest, "rb") as f:
				if f.read() == blob:
					return dest
		except OSError:
			pass
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
	if rtype in FONT_TYPES:
		return "fonts"
	if rtype in CURSOR_TYPES:
		return "cursors"
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
	# Item lengths come from the next item's offset (the last runs to total).
	lengths = {}
	for tag, off in items:
		nxt = min([o for _, o in items if o > off] + [total])
		lengths.setdefault((tag, off), max(0, nxt - off))
	patt = [(off, lengths[(tag, off)]) for tag, off in items if tag == "patt"]
	if patt:
		off, ln = patt[0]
		if pool + off + ln <= len(data):
			tags["patt"] = data[pool + off:pool + off + ln]
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


# Window-chrome skins: theme ppat names (any capitalisation) to bundle files.
# Only frame strips are used; buttons, scroll bars and menus keep ZacOS's drawing.
CHROME_PARTS = {
	"document-top": ("document window top", "window title", "doc window top"),
	"document-bottom": ("document window bottom", "window bottom", "doc window bottom"),
	"document-left": ("document window left", "window left", "doc window left"),
	"document-right": ("document window right", "window right", "doc window right"),
	"modal-top": ("modal top",),
	"modal-bottom": ("modal bottom",),
	"modal-left": ("modal left",),
	"modal-right": ("modal right",),
	"popup-tab": ("document window tab center",),
	"menu-background": ("menu background", "menu bar background",
		"list & menu background"),
	"menu-highlight": ("menu highlight", "menu(selected)"),
	"button": ("button",),
	"button-inactive": ("button inactive",),
	"button-pressed": ("button pressed",),
	"scroll-track-vert": ("vertical scroll track", "v scroll track",
		"vert scroll track"),
	"scroll-track-horz": ("horizontal scroll track", "h scroll track",
		"horz scroll track"),
	"scroll-thumb-horz-left": ("horz thumb left", "h thumb left"),
	"scroll-thumb-horz-stretch": ("horz thumb stretch", "h thumb stretch"),
	"scroll-thumb-horz-right": ("horz thumb right", "h thumb right"),
	"scroll-thumb-vert-top": ("vert thumb top", "v thumb top"),
	"scroll-thumb-vert-stretch": ("vert thumb stretch", "v thumb stretch"),
	"scroll-thumb-vert-bottom": ("vert thumb bottom", "v thumb bottom"),
	"progress-fill": ("progress bar innards", "progress bar innards active"),
	"progress-fill-inactive": ("progress bar innards inactive",),
}


def chrome_slot(name):
	"""The chrome file for a ppat resource name, or None (with -inactive)."""
	lower = name.lower()
	inactive = lower.endswith(" inactive")
	if inactive:
		lower = lower[:-len(" inactive")]
	for slot, names in CHROME_PARTS.items():
		if lower in names:
			return slot + ("-inactive" if inactive else "")
	return None


def process_resource_fork(data, out, counts, cluts, icons, assets, scen_tags, apple_menu, wctb, raw_icons, epics,
		theme_icons=None, system_file=False, cursors=None, file_icon=None, pxm=None,
		layo=None, plut=None):
	for rtype, res_id, body in parse_resource_fork(data):
		bucket = bucket_for(rtype)
		if not bucket:
			continue
		# The raw resource stays in the bundle for reference beside the
		# decoded files write_assets makes from it.
		write_unique(os.path.join(out, bucket), "%s_%d.bin" % (rtype.decode("latin1"), res_id), body)
		counts[bucket] += 1
		if rtype == b"clut":
			cols = parse_clut(body)
			if cols:
				cluts.append((res_id, cols))
		if rtype == b"scen":
			collection = decode_scen_collection(body)
			if "epic" in collection:
				epics.add(collection["epic"])
			if "patt" in collection and "patt" not in assets:
				assets["patt"] = collection["patt"]
			for tag, value in collection.items():
				if tag not in scen_tags and tag != "patt":
					scen_tags[tag] = value
		if rtype in (b"icl8", b"icl4", b"ICN#", b"icns"):
			if rtype != b"icns":
				icons.setdefault(rtype, []).append(body)
			# The theme file's own Finder icon (family 1024, the mini
			# window art): recorded only on the theme file's own pass,
			# never from the helpers.
			if file_icon is not None and res_id == 1024:
				file_icon.setdefault(rtype, []).append(body)
		# Kaleidoscope window art, same pass rule: the helpers carry none.
		if pxm is not None and rtype == b"pxm#":
			pxm.setdefault(res_id, []).append(body)
		if layo is not None and rtype == b"layo":
			layo[res_id] = body
		if plut is not None and rtype == b"plut":
			plut[res_id] = body
		if rtype in (b"icns", b"ics8", b"ics4", b"ics#") and res_id == -16386:
			apple_menu.setdefault(rtype, []).append(body)
		if rtype == b"wctb" and res_id == 0 and not wctb:
			wctb.update(decode_wctb(body))
		if rtype in (b"cicn", b"crsr", b"CURS"):
			raw_icons.setdefault(rtype, {})[res_id] = body
		if rtype == b"PICT":
			assets.setdefault("pict", []).append((res_id, body))
		if rtype in (b"PAT ", b"PAT#"):
			assets.setdefault("pat", []).append(body)
		if rtype == b"ppat":
			assets.setdefault("ppat", []).append((res_id, body))
			if "chrome" not in assets:
				# The ppat names live in the resource map, which
				# parse_resource_fork drops; read them here instead.
				# Several resources can match one slot (full-size and
				# "small" variants); the first alias listed wins.
				assets["chrome"] = {}
				try:
					named = macfont.resources(data).get("ppat", {})
				except (ValueError, struct.error, IndexError):
					named = {}
				lowered = {}
				for _rid, (pname, pbody) in named.items():
					lowered.setdefault(pname.lower(), pbody)
				for slot, names in CHROME_PARTS.items():
					for alt in names:
						if alt in lowered:
							assets["chrome"][slot] = lowered[alt]
							break
					if slot + "-inactive" not in assets["chrome"]:
						for alt in names:
							if alt + " inactive" in lowered:
								assets["chrome"][slot + "-inactive"] = lowered[alt + " inactive"]
								break
		if rtype == b"snd ":
			assets.setdefault("snd", []).append(body)
		if rtype in FONT_TYPES:
			assets.setdefault("font", []).append(body)
		# Theme icon families come from the System and Finder files only:
		# other files' same-numbered icons are unrelated.
		if system_file and theme_icons is not None and (rtype, res_id) in THEME_ICON_IDS:
			theme_icons.setdefault((rtype, res_id), []).append(body)
		if system_file and cursors is not None and (rtype, res_id) in CURSOR_IDS:
			cursors.setdefault((rtype, res_id), []).append(body)


def find_mac_file(fork_path, name):
	"""Locate a Mac OS 9 System Folder file (System, Finder, ...) for a
	theme file's fork.

	Climbs from the theme file to a folder holding `name` (raw or Netatalk
	`.rsrc`), else falls back to ~/Classic. Returns the fork's path, or
	None."""
	seen = set()
	d = os.path.abspath(os.path.dirname(fork_path))
	for _ in range(10):
		if d in seen:
			break
		seen.add(d)
		for candidate in (os.path.join(d, name), os.path.join(d, ".rsrc", name)):
			try:
				with open(candidate, "rb") as f:
					head = f.read(16)
			except OSError:
				continue
			if looks_like_resource_fork(open(candidate, "rb").read()):
				return candidate
		parent = os.path.dirname(d)
		if parent == d:
			break
		d = parent
	for candidate in (os.path.expanduser("~/Classic/System Folder/" + name),
			os.path.expanduser("~/Classic/System Folder/.rsrc/" + name)):
		try:
			if looks_like_resource_fork(open(candidate, "rb").read()):
				return candidate
		except OSError:
			continue
	return None


def fonts_dir_for(system_fork):
	"""The System Folder's Fonts folder next to a System fork, or None."""
	d = os.path.dirname(os.path.abspath(system_fork))
	if os.path.basename(d) == ".rsrc":
		d = os.path.dirname(d)
	candidate = os.path.join(d, "Fonts")
	return candidate if os.path.isdir(candidate) else None


def collect_fonts_dir(fonts_dir, assets):
	"""Add a Fonts folder's sfnt/FOND/nfnt resources, for --file mode."""
	def take(data):
		for rtype, res_id, body in parse_resource_fork(data):
			if rtype in FONT_TYPES:
				assets.setdefault("font", []).append(body)
	for root, _dirs, files in os.walk(fonts_dir):
		for fn in files:
			full = os.path.join(root, fn)
			try:
				with open(full, "rb") as f:
					raw = f.read()
			except OSError:
				continue
			if fn.startswith("._"):
				fork = extract_apple_double(raw)
				if fork:
					take(fork)
			elif looks_like_resource_fork(raw):
				take(raw)


def collect_helper_fork(path, cluts, apple_menu, theme_icons, cursors):
	"""Add a System/Finder file's Apple menu icon (ics -16386), colour
	tables, theme icon families and pointer shapes, for --file mode which
	otherwise never sees them."""
	try:
		with open(path, "rb") as f:
			data = f.read()
	except OSError:
		return
	for rtype, res_id, body in parse_resource_fork(data):
		if rtype in (b"icns", b"ics8", b"ics4", b"ics#") and res_id == -16386:
			apple_menu.setdefault(rtype, []).append(body)
		elif rtype == b"clut":
			try:
				cols = parse_clut(body)
			except (ValueError, IndexError, struct.error):
				continue
			if cols:
				cluts.append((res_id, cols))
		elif (rtype, res_id) in THEME_ICON_IDS:
			theme_icons.setdefault((rtype, res_id), []).append(body)
		elif (rtype, res_id) in CURSOR_IDS:
			cursors.setdefault((rtype, res_id), []).append(body)


def write_apple_menu(apple_menu, cluts, out, theme_apple=None, theme_cluts=()):
	"""Write apple-menu.png from the menu-bar Apple icon (id -16386).

	Whoever supplies the icon, it decodes against the System tables
	(the largest wins: a gray ramp would flatten the rainbow stripes)."""
	# The icon decodes against the System tables even when the theme
	# file supplies it (a scheme's own tables describe its chrome).
	# The rainbow stripes need the fullest table: gray ramps flatten them.
	helpers = cluts[len(theme_cluts):] or cluts
	palette = None
	for _res_id, cols in helpers:
		if cols and (palette is None or len(cols) > len(palette)):
			palette = cols
	# The theme's own Apple menu icon (an icon suite, usually) wins, as it
	# does for every other standard icon; the System's low-depth ics8/ics4
	# is the fallback. One resolver for both.
	icons = {(rtype, -16386): bodies for rtype, bodies in apple_menu.items()}
	rows = decode_standard_icon(icons, palette, -16386, 16)
	if rows is None:
		return
	pngwrite.write_png(os.path.join(out, "apple-menu.png"), 16, 16, rows)
	return rows


# Desktop pictures are screen-sized; icon-sized PICTs (window art, logo
# pictures, thumbnails) are not wallpapers and stay out of the Wallpaper
# folder. The raw resources are still saved under icons/ for reference.
WALLPAPER_MIN_SIDE = 128


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


def write_theme_fonts(scen_tags, system_path, fonts_dir, out):
	"""Write the fonts the theme asks for: fonts/views.pfont (its views font at
	its views size) and fonts/system.pfont (its system font at 12 points). A
	bitmap strike is used when there is one for that size; otherwise the
	family's TrueType font is rasterised at that size. Families not found are
	left to ZacOS's own fonts. Family names come from the System file and the Fonts
	folder's resource forks (<Fonts>/.rsrc/<name>)."""
	sources = []
	if system_path:
		with open(system_path, "rb") as f:
			raw = f.read()
		fork = extract_apple_double(raw) if system_path.endswith("._") or raw[:4] == b"\x00\x05\x16\x07" else raw
		sources.append(fork)
	rsrc = os.path.join(fonts_dir, ".rsrc") if fonts_dir else None
	if rsrc and os.path.isdir(rsrc):
		for name in sorted(os.listdir(rsrc)):
			try:
				with open(os.path.join(rsrc, name), "rb") as f:
					sources.append(f.read())
			except OSError:
				continue
	families = {}  # family name -> every resource map that holds it
	for fork in sources:
		try:
			res = macfont.resources(fork)
		except (ValueError, struct.error, IndexError):
			continue
		for family in macfont.families(res):
			families.setdefault(family, []).append(res)
	wants = (("views.pfont", scen_tags.get("vfnt"), scen_tags.get("vfsz", 10)),
		("system.pfont", scen_tags.get("lgsf"), 12))
	for filename, family, size in wants:
		if not family or family not in families:
			continue
		text = None
		for res in families[family]:
			strike = macfont.strikes(res, family).get((size, 0))
			try:
				if strike is not None:
					text = macfont.pfont_text(family, size, strike)
				else:
					sfnt = macfont.scalable(res, family)
					if sfnt is not None:
						text = macfont.pfont_from_sfnt(family, size, sfnt)
			except (ValueError, IndexError, struct.error, OSError):
				text = None
			if text is not None:
				break
		if text is None:
			continue
		os.makedirs(os.path.join(out, "fonts"), exist_ok=True)
		with open(os.path.join(out, "fonts", filename), "w", encoding="utf-8") as f:
			f.write(text)


def write_system_sounds(system_raw, out):
	"""Write the System file's alert sounds as sounds/system-sound-N.wav, in
	resource ID order. The installer never picks these as the default alert."""
	for i, (_rid, body) in enumerate(sorted(system_raw.get("snd ", {}).items())):
		try:
			macsnd.snd_to_wav(body, os.path.join(out, "sounds", "system-sound-%d.wav" % (i + 1)))
		except (ValueError, struct.error):
			continue


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


def write_desktop_wallpaper(scen_tags, pictures_dir, out):
	"""The desktop picture a theme names (scen dpnm): find it under the Desktop
	Pictures folder and write wallpapers/desktop-wallpaper.<ext>. A JPEG/PNG
	data fork is copied as-is; otherwise the .rsrc companion's PICT is decoded."""
	name = scen_tags.get("dpnm")
	if not name or not pictures_dir or not os.path.isdir(pictures_dir):
		return False
	found = None
	for root, _dirs, files in os.walk(pictures_dir):
		if os.path.basename(root) in (".finf", ".rsrc"):
			continue
		if name in files:
			found = os.path.join(root, name)
			break
	if not found:
		return False
	try:
		with open(found, "rb") as f:
			data = f.read()
	except OSError:
		return False
	os.makedirs(os.path.join(out, "wallpapers"), exist_ok=True)
	# Recognise the picture formats a Mac OS 9 Desktop Pictures folder can
	# hold. Anything but PNG is converted by the Theme Installer when it
	# installs: the desktop loads wallpapers as PNG.
	if data[:2] == b"\xff\xd8":
		ext = "jpg"
	elif data[:8] == b"\x89PNG\r\n\x1a\n":
		ext = "png"
	elif data[:6] in (b"GIF87a", b"GIF89a"):
		ext = "gif"
	elif data[:4] in (b"II*\x00", b"MM\x00*"):
		ext = "tif"
	elif data[:2] == b"BM":
		ext = "bmp"
	else:
		ext = None
	if ext:
		with open(os.path.join(out, "wallpapers", "desktop-wallpaper." + ext), "wb") as f:
			f.write(data)
		return True
	# A PICT whose picture is the data fork (no .rsrc companion): the
	# 512-byte PICT file header precedes the same opcodes a resource body
	# holds, so try the body with and without it.
	for body in ([data[512:]] if len(data) > 512 else []) + [data]:
		try:
			w, h, rows = macpict.decode_pict(body)
		except (ValueError, IndexError, struct.error):
			continue
		if w >= WALLPAPER_MIN_SIDE and h >= WALLPAPER_MIN_SIDE:
			pngwrite.write_png(os.path.join(out, "wallpapers", "desktop-wallpaper.png"),
				w, h, rows)
			return True
	rsrc = os.path.join(os.path.dirname(found), ".rsrc", name)
	try:
		with open(rsrc, "rb") as f:
			fork = f.read()
	except OSError:
		return False
	try:
		items = parse_resource_fork(fork)
	except (ValueError, struct.error, IndexError):
		return False
	picts = [b for t, _i, b in items if t == b"PICT"]
	if not picts:
		return False
	try:
		w, h, rows = macpict.decode_pict(picts[0])
	except (ValueError, IndexError, struct.error):
		return False
	pngwrite.write_png(os.path.join(out, "wallpapers", "desktop-wallpaper.png"), w, h, rows)
	return True


def write_assets(assets, epics, out):
	"""Decode PICT/PAT/snd resources into wallpapers, patterns and sounds."""
	# Other PICTs in a theme file are its credit splash (id 128) and the Appearance
	# panel's preview thumbnails (the scen epic ids), never desktop wallpapers.
	for res_id, body in assets.get("pict", []):
		if res_id == 128 or res_id in epics:
			continue
		try:
			w, h, rows = macpict.decode_pict(body)
			if w < WALLPAPER_MIN_SIDE or h < WALLPAPER_MIN_SIDE:
				continue
			pngwrite.write_png(os.path.join(out, "wallpapers", "picture-%d.png" % res_id), w, h, rows)
		except (ValueError, IndexError, struct.error):
			continue
	# The desktop pattern lives inside the scen collection ('patt'), not in the
	# theme's widget-skin ppat resources.
	if assets.get("patt"):
		try:
			w, h, rows = macpat.decode_ppat(assets["patt"])
			pngwrite.write_png(os.path.join(out, "patterns", "desktop-pattern.png"), w, h, rows)
		except (ValueError, IndexError, struct.error):
			pass
	# Desktop pattern tiles from PAT.
	for i, body in enumerate(assets.get("pat", [])):
		try:
			rows = macpat.decode_pat(body)
			pngwrite.write_png(os.path.join(out, "patterns", "pattern-%d.png" % (i + 1)), 8, 8, rows)
		except (ValueError, IndexError):
			continue
	# Window-chrome frame strips from the named ppat skins.
	for slot, body in sorted(assets.get("chrome", {}).items()):
		try:
			w, h, rows = macpat.decode_ppat(body)
			pngwrite.write_png(os.path.join(out, "chrome", "%s.png" % slot), w, h, rows)
		except (ValueError, IndexError, struct.error):
			continue
	# Colour pattern tiles from ppat.
	for i, (_pid, body) in enumerate(assets.get("ppat", [])):
		try:
			w, h, rows = macpat.decode_ppat(body)
			pngwrite.write_png(os.path.join(out, "patterns", "colour-pattern-%d.png" % (i + 1)), w, h, rows)
		except (ValueError, IndexError, struct.error):
			continue
	# Pixel patterns from ppat (Kaleidoscope colour tiles, same format
	# as the System file's own desktop ppats), numbered on after PATs.
	base = len(assets.get("pat", []))
	for j, (_pid, body) in enumerate(assets.get("ppat", [])):
		try:
			w, h, rows = macpat.decode_ppat(body)
			pngwrite.write_png(os.path.join(out, "patterns", "pattern-%d.png" % (base + j + 1)),
				w, h, rows)
		except (ValueError, IndexError, struct.error):
			continue
	# Alert sounds from snd.
	for i, body in enumerate(assets.get("snd", [])):
		try:
			macsnd.snd_to_wav(body, os.path.join(out, "sounds", "sound-%d.wav" % (i + 1)))
		except (ValueError, struct.error):
			continue
	# TrueType faces from sfnt, named by the font itself (FOND/nfnt-only
	# suitcases stay raw under fonts/ for reference). Suitcases repeat
	# the same face, so identical bodies install once. Returns the sorted
	# family names, recorded in theme.conf.
	taken = set()
	seen = set()
	families = set()
	files = {}
	font_dir = os.path.join(out, "fonts")
	for body in assets.get("font", []):
		try:
			# Shaper-safe bytes: Apple's AAT layout tables break HarfBuzz
			# shaping, so programs would get no text at all. Names survive.
			blob = macfont.sanitize_sfnt(body)
			digest = hashlib.sha256(blob).digest()
			if digest in seen:
				continue
			seen.add(digest)
			family, subfamily = macfont.parse_sfnt_names(blob)
			dest = write_unique(font_dir, macfont.ttf_filename(family, subfamily, taken), blob)
			families.add(family)
			files.setdefault(family, dest)
		except (ValueError, struct.error, UnicodeDecodeError):
			continue
	return sorted(families), files


def decode_pxm(body, layer=0, cluts=None):
	"""Decode one layer of a Kaleidoscope pxm# tile to RGBA rows.

	Layout: a 24-byte header (version, mask count, QuickDraw bounds
	top/left/bottom/right, bit depth 2/4/8/16, flags, 0, clut id, layer
	count), then `masks` 1-bit masks (16-bit word-aligned rows), then
	`layerCount` pixel layers (16-bit word-aligned rows)."""
	if len(body) < 24:
		raise ValueError("pxm# is too short")
	_ver, nmask, top, left, bot, right, bpp, _fl, _z0, _z1, cid, nl = struct.unpack(">12H", body[:24])
	h = bot - top
	w = right - left
	if w <= 0 or h <= 0 or layer < 0 or layer >= nl or bpp not in (2, 4, 8, 16):
		raise ValueError("pxm# has invalid geometry or layer")
	masks = nl if nmask == 0 else nmask
	mask_rb = ((w + 15) // 16) * 2
	pix_rb = ((w * bpp + 15) // 16) * 2
	mlayer = min(layer, max(0, masks - 1))
	moff = 24 + mlayer * mask_rb * h
	poff = 24 + masks * mask_rb * h + layer * pix_rb * h
	if len(body) < poff + pix_rb * h:
		raise ValueError("pxm# layer is too short")
	pal = (cluts or {}).get(cid, {})
	rows = []
	for y in range(h):
		row = []
		mbase = moff + y * mask_rb
		pbase = poff + y * pix_rb
		for x in range(w):
			if not (body[mbase + (x // 8)] & (0x80 >> (x % 8))):
				row.append((0, 0, 0, 0))
				continue
			if bpp == 16:
				v = struct.unpack(">H", body[pbase + x * 2:pbase + x * 2 + 2])[0]
				rgb = (((v >> 10) & 31) * 255 // 31,
					((v >> 5) & 31) * 255 // 31, (v & 31) * 255 // 31)
			elif bpp == 8:
				idx = body[pbase + x]
				rgb = pal.get(idx, (idx, idx, idx))
			elif bpp == 4:
				byte = body[pbase + (x // 2)]
				idx = (byte >> 4) if (x % 2 == 0) else (byte & 0xF)
				rgb = pal.get(idx, (idx * 17,) * 3)
			else:
				byte = body[pbase + (x // 4)]
				idx = (byte >> (6 - 2 * (x % 4))) & 3
				rgb = pal.get(idx, (idx * 85,) * 3)
			row.append(rgb + (255,))
		rows.append(row)
	return rows


def _parse_layo_records(body):
	"""Return the sequence of ('pxm#'|'plut', signed_id) records from a layo body."""
	first = -1
	for off in range(len(body) - 4):
		if body[off:off + 4] in (b"pxm#", b"plut", b"NULL", b"ppat"):
			first = off
			break
	if first < 20:
		return []
	recs = []
	off = first - 20
	while off + 40 <= len(body):
		tag = body[off + 20:off + 24]
		rid = struct.unpack(">h", body[off + 24:off + 26])[0]
		if tag in (b"pxm#", b"plut"):
			recs.append((tag, rid))
		off += 40
	return recs


def _parse_layo_records_ex(body):
	"""Like _parse_layo_records, but keeping each record's group word
	(pre[1]: the frame layout's active/inactive selector) and lead word
	(pre[4]: the title buttons' zone — 38 follows the title group)."""
	first = -1
	for off in range(len(body) - 4):
		if body[off:off + 4] in (b"pxm#", b"plut", b"NULL", b"ppat"):
			first = off
			break
	if first < 20:
		return []
	recs = []
	off = first - 20
	while off + 40 <= len(body):
		pre = struct.unpack(">10h", body[off:off + 20])
		tag = body[off + 20:off + 24]
		rid = struct.unpack(">h", body[off + 24:off + 26])[0]
		if tag in (b"pxm#", b"plut"):
			recs.append((tag, rid, pre[1], pre[4]))
		off += 40
	return recs


def _title_buttons_follow_group(layo_body):
	"""Whether the theme's zoom/grow boxes ride with the title group
	(left cluster) rather than pinning to the bar's right edge: every
	zoom/grow record in layo 128 sits in the title group's zone (38).
	Mixed zones keep the Platinum right-hand boxes."""
	recs = _parse_layo_records_ex(layo_body)
	zones = set()
	for tag, rid, group, lead in recs:
		if tag == b"pxm#" and (rid & 0xFFFF) in (210, 220):
			zones.add(lead)
	return bool(zones) and zones == {38}


def _parse_plut_entries(body):
	"""Return [(ppat_id, (r, g, b)), ...] from a plut 387 resource."""
	if not body or len(body) < 8:
		return []
	n = struct.unpack(">H", body[6:8])[0]
	out = []
	for k in range(n):
		off = 8 + k * 8
		if off + 8 > len(body):
			break
		pid, r, g, b = struct.unpack(">HHHH", body[off:off + 8])
		out.append((pid, (r >> 8, g >> 8, b >> 8)))
	return out


def write_theme_chrome(name, pxm, ppats, out, cluts=(), layo=None, plut=None):
	"""Decode the theme's own window art into the bundle's chrome/.

	Every Kaleidoscope scheme carrying standard window resources gets its
	buttons (pxm# 200 close, 210 collapse/grow, 220 zoom: layer 0 normal,
	layer 1 pressed) and its title-bar tiles from layo 128 (active, layer
	0) and layo 129 (inactive, layer 1) + plut 387. Synthetic fixtures
	without layo fall back to THEME_CHROME[name]."""
	patterns = dict(ppats)
	clut_map = {cid: dict(cols) for cid, cols in cluts}
	chrome_dir = os.path.join(out, "chrome")
	keys = {}

	def save_tile(key, rows):
		if not rows or not rows[0]:
			return
		os.makedirs(chrome_dir, exist_ok=True)
		fn = key + ".png"
		pngwrite.write_png(os.path.join(chrome_dir, fn), len(rows[0]), len(rows), rows)
		pngwrite.write_argb(os.path.join(chrome_dir, key + ".argb"), rows)
		keys[key] = "chrome/" + key + ".argb"

	def save_bar(key, rows, height):
		"""Save a title-bar tile cropped to the bar band.

		The frame layout's end caps and middle come from different
		resources, and a cap can carry transparent rows above the bar (or
		the window frame below it). Drawn from row 0, such a cap looks
		shorter than the middle. Start every tile at the first row that
		has ink and keep the middle's height, repeating the last row when
		the art is shorter."""
		if not rows or not rows[0]:
			return
		top = 0
		for y, row in enumerate(rows):
			if any(px[3] for px in row):
				top = y
				break
		rows = rows[top:top + height]
		if not rows:
			return
		while len(rows) < height:
			rows.append(list(rows[-1]))
		save_tile(key, rows)

	# 1. Universal Kaleidoscope title-bar buttons (200 close, 210 collapse, 220 zoom).
	has_any_btn = any(pid in pxm for pid in (200, 210, 220))
	if has_any_btn and (layo or name in THEME_CHROME):
		for short, pid in (("close", 200), ("grow", 210), ("zoom", 220)):
			bodies = pxm.get(pid)
			if not bodies:
				continue
			for layer, suffix in ((0, ""), (1, "-pressed")):
				try:
					rows = decode_pxm(bodies[0], layer, clut_map)
				except (ValueError, IndexError, struct.error):
					continue
				save_tile("button-" + short + suffix, rows)

	# 2. Generic layo 128 (active) and layo 129 (inactive) title-bar assembly.
	if layo and 128 in layo:
		plut_entries = _parse_plut_entries((plut or {}).get(387))
		mids_by_suffix = {}

		def resolve_rec(tag, rid, layer):
			if tag == b"pxm#":
				bodies = pxm.get(rid & 0xFFFF)
				if not bodies:
					return None
				try:
					return decode_pxm(bodies[0], layer, clut_map)
				except (ValueError, IndexError, struct.error):
					return None
			idx = ~rid
			if 0 <= idx < len(plut_entries):
				pid, rgb = plut_entries[idx]
				if pid and pid in patterns:
					try:
						_, _, rows = macpat.decode_ppat(patterns[pid])
						return rows
					except (ValueError, IndexError, struct.error):
						pass
				return [[rgb + (255,)] * 8 for _ in range(22)]
			return None

		for lid, layer, suffix in ((128, 0, ""), (129, 1, "-inactive")):
			if lid not in layo:
				continue
			recs = _parse_layo_records(layo[lid])
			if len(recs) < 3:
				continue
			left_rows = resolve_rec(recs[0][0], recs[0][1], layer)
			mid_rows = resolve_rec(recs[1][0], recs[1][1], layer)
			right_rec = next(
				(r for r in recs[2:] if r[0] == b"pxm#" and (r[1] & 0xFFFF) == 6173),
				next(
					(r for r in recs[2:] if r[0] == b"pxm#" and (r[1] & 0xFFFF) in (6178, 6134, 6278)),
					recs[2]))
			right_rows = resolve_rec(right_rec[0], right_rec[1], layer)
			bar_h = len(mid_rows)
			save_bar("bar-left" + suffix, left_rows, bar_h)
			save_bar("bar-middle" + suffix, mid_rows, bar_h)
			save_bar("bar-right" + suffix, right_rows, bar_h)
			mids_by_suffix[suffix] = mid_rows

		# 2b. Two-part bars: the title group rides a pinstripe patch on
		# the left while a span fills the rest of the bar. The span is
		# the wide pattern in the window-frame layout (layo 430,
		# group 1 active / group 0 inactive); the patch stays the
		# layo 128 middle. Gated on the left-cluster rule so themes
		# whose buttons pin right keep their single-span bar.
		def frame_span(layer):
			body = (layo or {}).get(430)
			if not body:
				return None
			best = None
			for tag, rid, group, lead in _parse_layo_records_ex(body):
				if tag != b"plut" or (group != layer and group != 256):
					continue
				idx = ~rid
				if 0 <= idx < len(plut_entries):
					pid, rgb = plut_entries[idx]
					if pid and pid in patterns:
						try:
							_, _, rows = macpat.decode_ppat(patterns[pid])
						except (ValueError, IndexError, struct.error):
							continue
						if len(rows[0]) >= 48 and len(rows) <= 32:
							if best is None or len(rows[0]) > len(best[0]):
								best = rows
			return best

		if 128 in (layo or {}) and _title_buttons_follow_group(layo[128]):
			keys["title-cluster"] = "left"
			for layer, suffix in ((1, ""), (0, "-inactive")):
				span_rows = frame_span(layer)
				mid_key = "bar-middle" + suffix
				if span_rows and keys.get(mid_key) and span_rows != mids_by_suffix.get(suffix):
					save_tile("bar-span" + suffix, span_rows)

		# 2c. Movable-modal dialogs (layo 134 active, 135 inactive):
		# pinstripe bars with their own close box (pxm# 6185) and small
		# zoom/grow boxes (pxm# 211/221). Caps and middle follow the
		# same roles as documents: first full-height side, first wide
		# middle, same right-cap ids.
		def dialog_middle(records, layer):
			for i, (tag, rid) in enumerate(records):
				if i == 0:
					continue
				if tag == b"pxm#":
					bodies = pxm.get(rid & 0xFFFF)
					if not bodies:
						continue
					try:
						rows = decode_pxm(bodies[0], layer, clut_map)
					except (ValueError, IndexError, struct.error):
						continue
					if len(rows[0]) >= 48:
						return rows
				else:
					rows = resolve_rec(tag, rid, layer)
					if rows and len(rows[0]) >= 48 and len(rows) >= 16:
						return rows
			if len(records) > 1:
				return resolve_rec(records[1][0], records[1][1], layer)
			return None

		def dialog_side(records, layer, last):
			cands = []
			for i, (tag, rid) in enumerate(records):
				if tag != b"pxm#" or (rid & 0xFFFF) in (200, 210, 220, 6185, 201, 211, 221):
					continue
				bodies = pxm.get(rid & 0xFFFF)
				if not bodies:
					continue
				try:
					rows = decode_pxm(bodies[0], layer, clut_map)
				except (ValueError, IndexError, struct.error):
					continue
				cands.append(rows)
				if i > 2:
					break
			full = [r for r in cands if len(r) >= 16]
			if not full:
				return None
			return full[-1] if last else full[0]

		for lid, layer, suffix in ((134, 0, ""), (135, 1, "-inactive")):
			if lid not in (layo or {}):
				continue
			recs = _parse_layo_records(layo[lid])
			if len(recs) < 2:
				continue
			left_rows = dialog_side(recs, layer, False)
			mid_rows = dialog_middle(recs, layer)
			right_rec = next(
				(r for r in recs[2:] if r[0] == b"pxm#" and (r[1] & 0xFFFF) == 6173),
				next(
					(r for r in recs[2:] if r[0] == b"pxm#" and (r[1] & 0xFFFF) in (6178, 6134, 6278)),
					None))
			right_rows = resolve_rec(right_rec[0], right_rec[1], layer) if right_rec else None
			if not right_rows:
				right_rows = dialog_side(recs, layer, True)
			bar_h = len(mid_rows)
			save_bar("bar-dialog-left" + suffix, left_rows, bar_h)
			save_bar("bar-dialog-middle" + suffix, mid_rows, bar_h)
			save_bar("bar-dialog-right" + suffix, right_rows, bar_h)

		for short, pid in (("dialog-close", 6185), ("dialog-zoom", 211), ("dialog-grow", 221)):
			bodies = pxm.get(pid)
			if not bodies:
				continue
			for layer, suffix in ((0, ""), (1, "-pressed")):
				try:
					rows = decode_pxm(bodies[0], layer, clut_map)
				except (ValueError, IndexError, struct.error):
					continue
				save_tile("button-" + short + suffix, rows)
		return keys

	# 3. Fallback for synthetic unit-test fixtures without layo.
	mapping = THEME_CHROME.get(name)
	if not mapping:
		return keys
	for key, pid in mapping.items():
		if key.startswith("button-"):
			continue
		body = patterns.get(pid)
		if body is None:
			continue
		try:
			_, _, rows = macpat.decode_ppat(body)
		except (ValueError, IndexError, struct.error):
			continue
		save_tile(key, rows)
	return keys


def decode_cursor(body):
	"""Decode a 16-bit CURS resource into (rows of RGBA, (hotx, hoty)).

	Layout: 32 bytes of image, 32 of mask, then the hot spot point
	(v, h as big-endian words). Black where the image is set, white
	where only the mask is, transparent elsewhere."""
	if len(body) < 68:
		raise ValueError("CURS is too short")
	image, mask = body[:32], body[32:64]
	hot_y, hot_x = struct.unpack(">HH", body[64:68])
	rows = []
	for y in range(16):
		row = []
		for x in range(16):
			bit = 0x80 >> (x % 8)
			if image[y * 2 + x // 8] & bit:
				row.append((0, 0, 0, 255))
			elif mask[y * 2 + x // 8] & bit:
				row.append((255, 255, 255, 255))
			else:
				row.append((0, 0, 0, 0))
		rows.append(row)
	return rows, (hot_x, hot_y)


def write_theme_cursors(cursors, out):
	"""Decode the mapped pointer shapes into the bundle's cursors/.

	Hand-placed entries already in cursors.json (the ROM-only arrow) are
	kept: decoded faces overwrite their own names only."""
	import json
	cursors_dir = os.path.join(out, "cursors")
	meta = {}
	try:
		with open(os.path.join(cursors_dir, "cursors.json")) as _f:
			for _key, _value in json.load(_f).items():
				if isinstance(_value, dict) and _value.get("file"):
					meta[_key] = _value
	except (OSError, ValueError):
		pass
	for kind, _src, rtype, rid in CURSOR_MAP:
		bodies = cursors.get((rtype, rid))
		if not bodies:
			continue
		try:
			rows, hot = decode_cursor(bodies[0])
		except (ValueError, IndexError):
			continue
		os.makedirs(cursors_dir, exist_ok=True)
		pngwrite.write_png(os.path.join(cursors_dir, kind + "-16.png"), 16, 16, rows)
		meta[kind] = {"file": kind + "-16.png", "hot": list(hot)}
	if meta:
		with open(os.path.join(cursors_dir, "cursors.json"), "w") as f:
			json.dump(meta, f, indent=2, sort_keys=True)
			f.write("\n")


# Xcursor names each decoded pointer stands in for (aliases from
# assets/cursors/platinum-cursors.pcur, which stays the source of truth).
CURSOR_ALIASES = {
    "arrow": ["default", "left_ptr", "arrow", "top_left_arrow"],
    "xterm": ["text", "vertical-text", "ibeam", "xterm"],
    "crosshair": ["crosshair", "cross", "tcross", "cell", "plus"],
    "move": ["move", "fleur", "all-scroll", "size_all"],
    "watch": ["watch", "wait", "half-busy", "progress",
              "0426c94ea35c87780ff01dc239897213",
              "3ecb610c1fb204f4440ff48c40d99"],
    "hand1": ["hand1", "grab", "grabbing", "hand", "openhand"],
    "hand2": ["pointer", "hand2", "pointing_hand", "link"],
}


def _cursor_builder():
    """The Xcursor writer beside this script: flat beside it when
    installed, under cursors/ in the source tree."""
    import importlib.util
    base = os.path.dirname(os.path.abspath(__file__))
    for rel in ("build_cursors.py", os.path.join("cursors", "build_cursors.py")):
        path = os.path.join(base, rel)
        if os.path.isfile(path):
            spec = importlib.util.spec_from_file_location("zacos9_build_cursors", path)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            return mod
    raise ImportError("no Xcursor writer beside " + base)


def _read_png_rgba(path):
    """Read an 8-bit RGBA PNG to rows (hand-placed art included).

    The extractor's own writer emits filter 0, but files from other tools
    use the remaining filter types, so unfilter them all here."""
    import struct as _s
    import zlib
    with open(path, "rb") as f:
        blob = f.read()
    assert blob[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, width, height, raw = 8, None, None, b""
    while pos + 8 <= len(blob):
        (length,) = _s.unpack(">I", blob[pos:pos + 4])
        typ = blob[pos + 4:pos + 8]
        data = blob[pos + 8:pos + 8 + length]
        if typ == b"IHDR":
            width, height = _s.unpack(">II", data[:8])
            assert data[8] == 8 and data[9] == 6, "8-bit RGBA expected"
        elif typ == b"IDAT":
            raw += data
        elif typ == b"IEND":
            break
        pos += 12 + length
    px = zlib.decompress(raw)
    rows, stride = [], width * 4 + 1
    prev = bytearray(width * 4)
    for y in range(height):
        line = bytearray(px[y * stride + 1:y * stride + 1 + width * 4])
        kind = px[y * stride]
        if kind == 1:  # Sub: each byte adds its left neighbour.
            for x in range(4, len(line)):
                line[x] = (line[x] + line[x - 4]) & 0xFF
        elif kind == 2:  # Up: each byte adds the row above.
            for x in range(len(line)):
                line[x] = (line[x] + prev[x]) & 0xFF
        elif kind == 3:  # Average: the mean of left and above.
            for x in range(len(line)):
                a = line[x - 4] if x >= 4 else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif kind == 4:  # Paeth: the nearest of left, above, upper-left.
            for x in range(len(line)):
                a = line[x - 4] if x >= 4 else 0
                b, c = prev[x], prev[x - 4] if x >= 4 else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else b if pb <= pc else c
                line[x] = (line[x] + pr) & 0xFF
        else:
            assert kind == 0, "unknown filter %d" % kind
        rows.append([tuple(line[x:x + 4]) for x in range(0, len(line), 4)])
        prev = line
    return rows


def write_xcursor_theme(out):
    """Build an Xcursor theme from the bundle's pointers (decoded plus
    hand-placed): cursors/Macintosh/ with index.theme inheriting ZacOS9,
    so unmapped cursors keep the desktop's own art."""
    meta_path = os.path.join(out, "cursors", "cursors.json")
    try:
        with open(meta_path) as f:
            import json as _json
            meta = _json.load(f)
    except (OSError, ValueError):
        return
    if not meta:
        return
    try:
        builder = _cursor_builder()
    except (OSError, ImportError):
        return
    theme_dir = os.path.join(out, "cursors", "Macintosh")
    os.makedirs(os.path.join(theme_dir, "cursors"), exist_ok=True)
    for kind, entry in sorted(meta.items()):
        png = os.path.join(out, "cursors", entry.get("file", ""))
        hot = entry.get("hot", [0, 0])
        if not os.path.isfile(png):
            continue
        try:
            rows = _read_png_rgba(png)
        except (OSError, AssertionError, ValueError):
            continue
        size = len(rows)
        frames = {}
        for scale in builder.SCALES:
            dst = builder.pixels(scale)
            frame = []
            for y in range(dst):
                sy = min(size - 1, y * size // dst)
                for x in range(dst):
                    sx = min(size - 1, x * size // dst)
                    r, g, b, a = rows[sy][sx]
                    frame.append(a << 24 | r << 16 | g << 8 | b)
            frames[scale] = [frame]
        data = builder.xcursor(frames, (int(hot[0]), int(hot[1])), 0)
        for name in [kind] + CURSOR_ALIASES.get(kind, []):
            with open(os.path.join(theme_dir, "cursors", name), "wb") as f:
                f.write(data)
    with open(os.path.join(theme_dir, "index.theme"), "w") as f:
        f.write("[Icon Theme]\nName=Macintosh\nInherits=ZacOS9\n")


def write_theme_art(icons, apple_rows, cluts, out, theme_cluts=()):
	"""Write menu-icon.png for the Zac menu slot.

	The slot shows Mac OS 9's Apple menu icon when the user's System file
	supplied one (`apple_rows`, 16x16), else the theme file's own icon.
	Neither is startup art: Happy Zac and the Welcome logo keep their
	built-ins until a real Happy Mac / logo source from the user's own
	install is wired up (no Happy Mac ships with ZacOS). Hand-placed
	bundle files (boot/happy-zac-Nx.png, boot/zacos-logo-Nx.png) still
	override them. The fallback icon decodes against the System palette
	like every other icon; without a System file the theme's own tables
	stand in."""
	if apple_rows is not None:
		pngwrite.write_png(os.path.join(out, "menu-icon.png"), 20, 15,
			pngwrite.fit_scale(apple_rows, 16, 16, 20, 15))
		return
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

	palette = macicons.resolve_palette(cluts[len(theme_cluts):] or cluts)

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


def pattern_average_chrome(out):
	"""Approximate window-chrome colours from the theme's own desktop
	tile (its largest decoded pattern): the average ink, mixed toward
	white so titles stay readable on it.

	Kaleidoscope schemes store no flat chrome colours (their bars are
	composed artwork, which ZacOS does not skin), so this stands in where
	no true gray ramp exists. Returns {} when there is nothing to read.
	"""
	import glob as _glob
	try:
		from PIL import Image as _Image
	except ImportError:
		return {}
	files = [p for p in _glob.glob(os.path.join(out, "patterns", "pattern-*.png"))
		if os.path.isfile(p)]
	if not files:
		return {}
	biggest, area = None, 0
	for path in files:
		try:
			with _Image.open(path) as im:
				im = im.convert("RGB")
				a = im.width * im.height
				if a > area:
					biggest, area = list(im.getdata()), a
		except (OSError, ValueError):
			continue
	if not biggest:
		return {}

	def mix(rgb, other, amount):
		return "%02x%02x%02x" % tuple(
			max(0, min(255, round(c + (o - c) * amount))) for c, o in zip(rgb, other))

	n = len(biggest)
	avg = tuple(sum(px[i] for px in biggest) // n for i in range(3))
	base = mix(avg, (255, 255, 255), 0.5)
	face = mix(avg, (255, 255, 255), 0.25)
	dark = mix(avg, (0, 0, 0), 0.3)
	return {"title-base": base, "frame-face": face,
		"title-stripe-dark": dark, "frame-dark": dark}


def write_theme_pfonts(out, scen_tags, font_files):
	"""Rasterize the theme's window-chrome bitmap fonts from its own
	TrueType faces: the system font at text sizes, the views font at
	list sizes, each at 1x and the fractional scales. A face that will
	not rasterize is skipped, leaving the built-in bitmaps in place.
	Returns how many .pfont files were written."""
	written = 0
	for role, scen_key, sizes in (("system", "lgsf", (12, 15, 18, 21)),
			("views", "vfnt", (11, 14, 17, 19))):
		face = scen_tags.get(scen_key, "")
		path = font_files.get(face) if face else None
		if not path:
			continue
		for size in sizes:
			try:
				text = macfont.rasterize_pfont(path, size)
			except (ValueError, OSError):
				continue
			with open(os.path.join(out, "fonts", "theme-%s-%d.pfont" % (role, size)), "w") as f:
				f.write(text)
			written += 1
	return written


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
	for key in ("title-base", "title-stripe-light", "title-stripe-dark", "frame-light",
			"frame-dark", "frame-border", "frame-face"):
		parser.add_argument("--" + key, help="Window chrome colour as RRGGBB (else the theme's own)")
	parser.add_argument("--list-cluts", action="store_true", help="Print colour tables from clut resources and exit")
	parser.add_argument("--auto-colours", action="store_true", help="Heuristically pick chrome colours from the clut resources")
	parser.add_argument("--fonts", help="The Fonts folder (its .rsrc folder holds the font resource forks)")
	parser.add_argument("--system", help="The System file, for its busy, resize, move and hand pointers")
	parser.add_argument("--pictures", help="Desktop Pictures folder, for the theme's dpnm wallpaper")
	parser.add_argument("--dump-icons", action="store_true", help="Decode cicn/crsr/CURS resources to PNG and a text dump")
	args = parser.parse_args()

	out = os.path.abspath(args.output)
	for sub in ("icons", "patterns", "sounds", "theme", "boot", "wallpapers", "fonts", "cursors",
		"chrome"):
		os.makedirs(os.path.join(out, sub), exist_ok=True)

	counts = {"icons": 0, "patterns": 0, "sounds": 0, "theme": 0, "fonts": 0, "cursors": 0}
	cluts = []
	icons = {}
	assets = {}
	scen_tags = {}
	epics = set()
	apple_menu = {}
	wctb = {}
	raw_icons = {}
	theme_icons = {}
	cursors = {}
	# What the theme file itself supplied in --file mode (its families
	# and tables stay first and win); the helpers append after them.
	# --source mode leaves these empty: everything comes from the tree.
	theme_keys = set()
	theme_cluts = []
	theme_apple = {}
	file_icon = {}
	pxm = {}
	layo = {}
	plut = {}

	if args.file:
		# A single theme file the user copied out of Mac OS 9.
		filepath = os.path.abspath(args.file)
		if not os.path.isfile(filepath):
			print("file not found: " + filepath, file=sys.stderr)
			return 2
		base = os.path.basename(filepath)
		if base.startswith("._"):
			base = base[2:]
		for ext in (".rsrc", ".bin", ".theme"):
			if base.lower().endswith(ext):
				base = base[:-len(ext)]
				break
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
			# The theme file's own desktop icons and menu icon win over
			# the System/Finder helpers' (a Kaleidoscope scheme ships
			# its own art); the helpers fill the kinds it lacks.
			process_resource_fork(fork, out, counts, cluts, icons, assets, scen_tags, apple_menu, wctb, raw_icons, epics,
				theme_icons, True, cursors, file_icon, pxm, layo, plut)
			theme_keys = set(theme_icons.keys())
			theme_cluts = list(cluts)
			theme_apple = {key: len(bodies) for key, bodies in apple_menu.items()}
		if any(counts.values()):
			# Single-file mode never walks the tree, so fetch the Apple
			# menu icon and desktop icons from the user's System and
			# Finder files when they are around. Only when the chosen
			# file held artwork itself: an artless file stays refused
			# even with a System file present. The theme file's own
			# entries, if any, stay first and win.
			system = args.system or find_mac_file(filepath, "System")
			if system:
				collect_helper_fork(system, cluts, apple_menu, theme_icons, cursors)
				fonts = fonts_dir_for(system)
				if fonts:
					collect_fonts_dir(fonts, assets)
			# The Finder sits beside the System file, at the same level
			# (or under the same .rsrc folder); it holds the desktop's
			# volume icons.
			finder = (find_mac_file(system, "Finder") if system else None) or \
				find_mac_file(filepath, "Finder")
			if finder:
				collect_helper_fork(finder, cluts, apple_menu, theme_icons, cursors)
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
							base = fn[2:]
							helper = base in ("System", "Finder")
							process_resource_fork(fork, out, counts, cluts, icons, assets, scen_tags, apple_menu, wctb, raw_icons, epics,
								theme_icons, helper, cursors if helper else None, file_icon, pxm, layo, plut)
							continue
						with open(full, "rb") as f:
							raw = f.read()
						# Not AppleDouble after all: fall through.

					# A raw resource fork (not an AppleDouble) when the tail maps back.
					if looks_like_resource_fork(raw) and not fn.startswith("._"):
						helper = fn in ("System", "Finder")
						process_resource_fork(raw, out, counts, cluts, icons, assets, scen_tags, apple_menu, wctb, raw_icons, epics,
							theme_icons, helper, cursors if helper else None, file_icon, pxm, layo, plut)
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
						helper = fn in ("System", "Finder")
						process_resource_fork(raw, out, counts, cluts, icons, assets, scen_tags, apple_menu, wctb, raw_icons, epics,
							theme_icons, helper, cursors if helper else None, file_icon, pxm, layo, plut)

		for s in [src] + expand_archives(src):
			walk_tree(s)

	
	# The System file's Apple menu icon (ics8/ics4 id -16386), for themes
	# the user builds from their own Mac OS 9 install: it becomes both
	# apple-menu.png and the Zac menu icon. Without one, the theme file's
	# own icon fills the Zac menu slot instead.
	# A rebuild with a source that has lost an asset must not leave the
	# previous extraction's file in place.
	for stale in ("apple-menu.png", "menu-icon.png", "file-icon.png"):
		try:
			os.remove(os.path.join(out, stale))
		except FileNotFoundError:
			pass
	apple_rows = write_apple_menu(apple_menu, cluts, out, theme_apple, theme_cluts)
	write_theme_art(icons, apple_rows, cluts, out, theme_cluts)
	write_theme_icons(theme_icons, cluts, out, theme_keys, theme_cluts)
	write_file_icon(file_icon, cluts, theme_cluts, out)
	write_theme_cursors(cursors, out)
	write_xcursor_theme(out)
	# Pointers: the arrow family from the theme file, the rest from the System file.
	theme_raw = {k.decode("latin1"): v for k, v in raw_icons.items()}
	pointer_cluts = {}
	for _rid, cols in cluts:
		pointer_cluts.setdefault(len(cols), cols)
	system_raw, system_cluts = pointer_resources(args.system) if args.system else ({}, {})
	write_pointers(theme_raw, pointer_cluts, system_raw, system_cluts, out)
	write_system_sounds(system_raw, out)
	write_theme_fonts(scen_tags, args.system, args.fonts, out)
	if args.dump_icons:
		write_icon_dump(raw_icons, cluts, out)
	# Decode wallpapers, patterns and sounds.
	font_families, font_files = write_assets(assets, epics, out)
	chrome_keys = write_theme_chrome(name, pxm, assets.get("ppat", []), out,
		theme_cluts or cluts, layo, plut)
	write_desktop_wallpaper(scen_tags, args.pictures, out)

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
			# A real gray ramp, not a tinted one: entry 0 alone being
			# gray is not enough (a blue ramp starts light gray too and
			# dyes the whole chrome blue).
			if r0 == g0 == b0 and all(v[0] == v[1] == v[2] for _, v in cols):
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
		# The Appearance panel rebuilds a bundle whose extractor has
		# changed, so an updated ZacOS fills in assets an older one
		# could not decode.
		try:
			with open(os.path.abspath(__file__), "rb") as _self:
				f.write("extractor-sha256=%s\n" % hashlib.sha256(_self.read()).hexdigest())
		except OSError:
			pass
		accent = args.accent or auto_accent
		highlight = args.highlight or scen_highlight or auto_highlight
		pattern = args.pattern
		approx = pattern_average_chrome(out)
		def chrome(key, auto):
			explicit = getattr(args, key.replace("-", "_"), None)
			if explicit:
				return explicit
			return auto or approx.get(key)
		title_base = chrome("title-base", auto_title_base)
		title_stripe_light = chrome("title-stripe-light", auto_title_stripe_light)
		title_stripe_dark = chrome("title-stripe-dark", auto_title_stripe_dark)
		frame_light = chrome("frame-light", auto_frame_light)
		frame_dark = chrome("frame-dark", auto_frame_dark)
		frame_border = chrome("frame-border", auto_frame_border)
		frame_face = chrome("frame-face", auto_frame_face)
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
		if font_families:
			f.write("font-families=%s\n" % ",".join(font_families))
		if scen_tags.get("dpnm"):
			f.write("desktop-picture=%s\n" % scen_tags["dpnm"])
		if scen_tags.get("patn"):
			f.write("pattern-name=%s\n" % scen_tags["patn"])
		if scen_tags.get("hcnm"):
			f.write("highlight-name=%s\n" % scen_tags["hcnm"])
		if scen_tags.get("sndt"):
			f.write("sound-set=%s\n" % scen_tags["sndt"])
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
		for key in ("bar-left", "bar-middle", "bar-right", "bar-span",
				"bar-left-inactive", "bar-middle-inactive", "bar-right-inactive",
				"bar-span-inactive", "bar-dialog-left", "bar-dialog-middle",
				"bar-dialog-right", "bar-dialog-left-inactive",
				"bar-dialog-middle-inactive", "bar-dialog-right-inactive",
				"button-close", "button-close-pressed",
				"button-zoom", "button-zoom-pressed",
				"button-grow", "button-grow-pressed",
				"button-dialog-close", "button-dialog-close-pressed",
				"button-dialog-zoom", "button-dialog-zoom-pressed",
				"button-dialog-grow", "button-dialog-grow-pressed"):
			if chrome_keys.get(key):
				f.write("%s=%s\n" % (key, chrome_keys[key]))
		if chrome_keys.get("title-cluster"):
			f.write("title-cluster=%s\n" % chrome_keys["title-cluster"])

	write_theme_pfonts(out, scen_tags, font_files)

	print("Created theme bundle: " + out)
	print("  icons:    %d" % counts["icons"])
	print("  patterns: %d" % counts["patterns"])
	print("  sounds:   %d" % counts["sounds"])
	print("  theme:    %d" % counts["theme"])
	print("  fonts:    %d" % counts["fonts"])
	print("Note: Appearance theme (thme/clut/pltt) resources are saved raw under")
	print("theme/ for analysis; pass --accent/--highlight/--pattern to set")
	print("window chrome colours. Resource-fork assets must be extracted first")
	print("(e.g. with `macunpack` from the macutils package).")
	return 0


if __name__ == "__main__":
	sys.exit(main())
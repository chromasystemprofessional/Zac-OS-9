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
import sys

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
THEME_TYPES = {b"thme", b"clut", b"pltt", b"WIND"}


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


def process_resource_fork(data, out, counts, cluts, icons, assets):
	for rtype, res_id, body in parse_resource_fork(data):
		bucket = bucket_for(rtype)
		if not bucket:
			continue
		write_unique(os.path.join(out, bucket), "%s_%d.bin" % (rtype.decode("latin1"), res_id), body)
		counts[bucket] += 1
		if rtype == b"clut":
			cols = parse_clut(body)
			if cols:
				cluts.append((res_id, cols))
		if rtype in (b"icl8", b"icl4", b"ICN#"):
			icons.setdefault(rtype, []).append(body)
		if rtype == b"PICT":
			assets.setdefault("pict", []).append(body)
		if rtype in (b"PAT ", b"PAT#"):
			assets.setdefault("pat", []).append(body)
		if rtype == b"snd ":
			assets.setdefault("snd", []).append(body)


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
	args = parser.parse_args()

	out = os.path.abspath(args.output)
	for sub in ("icons", "patterns", "sounds", "theme", "boot", "wallpapers"):
		os.makedirs(os.path.join(out, sub), exist_ok=True)

	counts = {"icons": 0, "patterns": 0, "sounds": 0, "theme": 0}
	cluts = []
	icons = {}
	assets = {}

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
			process_resource_fork(fork, out, counts, cluts, icons, assets)
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

		for root, _dirs, files in os.walk(src):
			for fn in files:
				full = os.path.join(root, fn)
				ext = os.path.splitext(fn)[1].lower()
				with open(full, "rb") as f:
					raw = f.read()

				# AppleDouble files carry the resource fork.
				if fn.startswith("._"):
					fork = extract_apple_double(raw)
					if fork:
						process_resource_fork(fork, out, counts, cluts, icons, assets)
						continue
					# Not AppleDouble after all: fall through.

				# A raw resource fork (not an AppleDouble) when the tail maps back.
				if looks_like_resource_fork(raw) and not fn.startswith("._"):
					process_resource_fork(raw, out, counts, cluts, icons, assets)
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
		for root, _dirs, files in os.walk(src):
			if os.path.basename(root) != ".rsrc":
				continue
			for fn in files:
				full = os.path.join(root, fn)
				with open(full, "rb") as f:
					raw = f.read()
				if looks_like_resource_fork(raw):
					process_resource_fork(raw, out, counts, cluts, icons, assets)

	# Turn the theme's icon into the menu icon and boot art.
	write_theme_art(icons, cluts, out)
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

	with open(os.path.join(out, "theme.conf"), "w") as f:
		f.write("name=%s\n" % name)
		f.write("source=%s\n" % src)
		accent = args.accent or auto_accent
		highlight = args.highlight or auto_highlight
		pattern = args.pattern
		title_base = auto_title_base
		title_stripe_light = auto_title_stripe_light
		title_stripe_dark = auto_title_stripe_dark
		frame_light = auto_frame_light
		frame_dark = auto_frame_dark
		frame_border = auto_frame_border
		frame_face = auto_frame_face
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
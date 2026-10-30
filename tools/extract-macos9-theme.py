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

# Loose files are copied straight across.
ASSET_EXTS = {".icns", ".png", ".jpg", ".jpeg", ".wav", ".aiff", ".aif", ".pat"}

# Resource-fork types and where they land.
ICON_TYPES = {b"ICN#", b"icl8", b"icl4", b"ics#", b"ics8", b"ics4", b"icns", b"ICON"}
PATTERN_TYPES = {b"PAT ", b"PAT#"}
SOUND_TYPES = {b"snd "}
PICT_TYPES = {b"PICT"}


def parse_resource_fork(data):
	"""Yield (type, id, bytes) for each resource in a raw resource fork.

	The Mac OS 9 resource-fork format: resource data at the front, a resource
	map at the back; the map's type list points to per-type reference lists,
	each naming a resource id and its data offset. Returns a list."""
	items = []
	if len(data) < 4:
		return items
	map_offset = struct.unpack(">I", data[-4:])[0]
	if map_offset + 28 > len(data):
		return items
	# Resource map header (offsets from Inside Macintosh).
	data_offset, _map2, _data_len, _map_len = struct.unpack(">IIII", data[map_offset:map_offset + 16])
	type_list_off = struct.unpack(">H", data[map_offset + 24:map_offset + 26])[0]
	type_list = map_offset + type_list_off
	if type_list + 2 > len(data):
		return items
	num_types = struct.unpack(">H", data[type_list:type_list + 2])[0] + 1
	pos = type_list + 2
	for _ in range(num_types):
		if pos + 8 > len(data):
			break
		rtype = data[pos:pos + 4]
		num_res = struct.unpack(">H", data[pos + 4:pos + 6])[0] + 1
		ref_off = struct.unpack(">H", data[pos + 6:pos + 8])[0]
		pos += 8
		ref_list = type_list + ref_off
		if ref_list + 2 > len(data):
			continue
		rpos = ref_list + 2
		for _ in range(num_res):
			if rpos + 12 > len(data):
				break
			res_id = struct.unpack(">H", data[rpos:rpos + 2])[0]
			data_off_attr = struct.unpack(">I", data[rpos + 4:rpos + 8])[0]
			rpos += 12
			off = data_offset + (data_off_attr & 0x00FFFFFF)
			if off + 4 > len(data):
				continue
			length = struct.unpack(">I", data[off:off + 4])[0]
			body = data[off + 4:off + 4 + length]
			items.append((rtype, res_id, body))
	return items


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
	if len(data) < 4:
		return False
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
	return None


def process_resource_fork(data, out, counts):
	for rtype, res_id, body in parse_resource_fork(data):
		bucket = bucket_for(rtype)
		if not bucket:
			continue
		write_unique(os.path.join(out, bucket), "%s_%d.bin" % (rtype.decode("latin1"), res_id), body)
		counts[bucket] += 1


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--source", help="Mac OS 9 tree (defaults to ~/Classic)")
	parser.add_argument("--output", required=True, help="Destination theme bundle directory")
	parser.add_argument("--name", help="Theme name (defaults to the source folder's name)")
	args = parser.parse_args()

	src = args.source
	if not src:
		src = os.path.expanduser("~/Classic")
	if not os.path.isdir(src):
		print("source is not a directory: " + src, file=sys.stderr)
		print("Hint: set up Old Dog first (put your Mac OS 9 install in ~/Classic).", file=sys.stderr)
		return 2
	src = os.path.abspath(src)

	out = os.path.abspath(args.output)
	for sub in ("icons", "patterns", "sounds"):
		os.makedirs(os.path.join(out, sub), exist_ok=True)

	name = args.name or os.path.basename(src.rstrip(os.sep)) or "Mac OS 9 Theme"
	counts = {"icons": 0, "patterns": 0, "sounds": 0}

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
					process_resource_fork(fork, out, counts)
					continue
				# Not AppleDouble after all: fall through.

			# A raw resource fork (not an AppleDouble) when the tail maps back.
			if looks_like_resource_fork(raw) and not fn.startswith("._"):
				process_resource_fork(raw, out, counts)
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

	with open(os.path.join(out, "theme.conf"), "w") as f:
		f.write("name=%s\n" % name)
		f.write("source=%s\n" % src)

	print("Created theme bundle: " + out)
	print("  icons:    %d" % counts["icons"])
	print("  patterns: %d" % counts["patterns"])
	print("  sounds:   %d" % counts["sounds"])
	return 0


if __name__ == "__main__":
	sys.exit(main())
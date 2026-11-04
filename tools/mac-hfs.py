#!/usr/bin/env python3
"""Copy a Mac OS 9 folder, with both forks, out of an HFS or HFS+ image.

Old Dog keeps the user's Mac OS 9 install on an HFS or HFS+ disk image. The
kernel's HFS driver exposes only the data fork (resource forks disappeared
from Linux's HFS driver long ago), so a mounted copy loses the System file's
Apple menu icon, fonts and other resources. Read the image directly instead.

The reader is intentionally small: one volume (raw or in an Apple partition
map), the catalog and extents-overflow B-trees, and the file forks. It never
writes to the image.
"""

import argparse
import os
import struct
import sys

BLOCK = 512


class MacVolumeError(Exception):
	"""A disk image that could not be read as an HFS volume."""


def _u16(data, offset):
	return struct.unpack(">H", data[offset:offset + 2])[0]


def _u32(data, offset):
	return struct.unpack(">I", data[offset:offset + 4])[0]


def _u64(data, offset):
	return struct.unpack(">Q", data[offset:offset + 8])[0]


def _extents(data, offset, count):
	"""HFS+ fork extents: a 32-bit start block and block count."""
	out = []
	for i in range(count):
		start = _u32(data, offset + i * 8)
		blocks = _u32(data, offset + i * 8 + 4)
		if blocks:
			out.append((start, blocks))
	return out


def _extents16(data, offset, count):
	"""Classic HFS fork extents: a 16-bit start block and block count."""
	out = []
	for i in range(count):
		start = _u16(data, offset + i * 4)
		blocks = _u16(data, offset + i * 4 + 2)
		if blocks:
			out.append((start, blocks))
	return out


def _safe_name(name):
	# Classic Mac names may contain a slash; the Finder and Linux's HFS
	# driver show it as a colon. Use the same name in the copy.
	name = name.replace("/", ":")
	if not name or name in (".", "..") or "\0" in name:
		raise MacVolumeError("unsafe Mac file name: " + repr(name))
	return name


class _Volume:
	"""Common B-tree walking and file copying for HFS and HFS+."""

	def __init__(self, stream, base):
		self.stream = stream
		self.base = base
		self.image_size = os.fstat(stream.fileno()).st_size
		self.children = {}
		self.overflows = {}
		self._read_tree()

	def _read(self, offset, size):
		self.stream.seek(self.base + offset)
		data = self.stream.read(size)
		if len(data) != size:
			raise MacVolumeError("truncated disk image")
		return data

	def _fork(self, extents, size):
		if size <= 0:
			return b""
		if size > self.image_size:
			raise MacVolumeError("fork is larger than the disk image")
		out = bytearray()
		remaining = size
		for start, blocks in extents:
			if remaining <= 0:
				break
			take = min(blocks * self.block_size, remaining)
			out += self._read(self.blocks_start + start * self.block_size, take)
			remaining -= take
		if remaining:
			raise MacVolumeError("fork extents end before its size")
		return bytes(out)

	def _btree_records(self, tree, node_size, first_leaf):
		"""Yield (node, record_offset) for every leaf record."""
		seen = set()
		node_number = first_leaf
		while node_number and node_number not in seen:
			seen.add(node_number)
			node = tree[node_number * node_size:(node_number + 1) * node_size]
			if len(node) != node_size or node[8] != 0xFF:
				break
			count = _u16(node, 10)
			if count > (node_size - 14) // 2:
				raise MacVolumeError("invalid B-tree record count")
			for index in range(count):
				yield node, _u16(node, node_size - 2 * (index + 1))
			node_number = _u32(node, 0)

	def _folder(self, parent, name):
		for entry in self.children.get(parent, []):
			if entry["name"].casefold() == name.casefold() and entry["folder"]:
				return entry
		return None

	def _file(self, folder_id, name):
		for entry in self.children.get(folder_id, []):
			if entry["name"].casefold() == name.casefold() and not entry["folder"]:
				return entry
		return None

	def find_folder(self, path):
		parent = 2
		for part in [part for part in path.split("/") if part]:
			entry = self._folder(parent, part)
			if entry is None:
				raise MacVolumeError("no folder " + repr(part) + " in " + repr(path))
			parent = entry["id"]
		return parent

	def copy_tree(self, source, destination, merge=False):
		root = self.find_folder(source)
		self._copy_folder(root, destination, merge)

	def _copy_folder(self, folder_id, destination, merge):
		os.makedirs(destination, exist_ok=True)
		for entry in sorted(self.children.get(folder_id, []),
				key=lambda item: item["name"].casefold()):
			name = _safe_name(entry["name"])
			if name in (".rsrc", ".finf", ".DS_Store"):
				continue
			target = os.path.join(destination, name)
			if entry["folder"]:
				self._copy_folder(entry["id"], target, merge)
				continue
			if not merge or not os.path.exists(target):
				with open(target, "wb") as stream:
					stream.write(self.fork(entry, resource=False))
			resource = self.fork(entry, resource=True)
			if resource:
				forks = os.path.join(destination, ".rsrc")
				os.makedirs(forks, exist_ok=True)
				with open(os.path.join(forks, name), "wb") as stream:
					stream.write(resource)


class HFSVolume(_Volume):
	"""Classic HFS (the format Old Dog's blank 2 GB disk usually becomes)."""

	def __init__(self, stream, base):
		self.stream = stream
		self.base = base
		self.image_size = os.fstat(stream.fileno()).st_size
		self.stream.seek(base + 1024)
		mdb = self.stream.read(162)
		if len(mdb) != 162 or mdb[:2] != b"BD":
			raise MacVolumeError("not an HFS volume")
		self.block_size = _u32(mdb, 20)
		self.blocks_start = _u16(mdb, 28) * BLOCK
		if self.block_size < BLOCK or self.block_size % BLOCK:
			raise MacVolumeError("invalid HFS allocation block size")
		catalog_size = _u32(mdb, 146)
		extents_size = _u32(mdb, 130)
		self.catalog = self._fork(_extents16(mdb, 150, 3), catalog_size)
		self.extents = self._fork(_extents16(mdb, 134, 3), extents_size)
		super().__init__(stream, base)

	def _read_tree(self):
		if len(self.catalog) < 512:
			raise MacVolumeError("missing HFS catalog")
		node_size = _u16(self.catalog, 32)
		first_leaf = _u32(self.catalog, 24)
		if node_size < 512 or node_size % 2 or len(self.catalog) < node_size:
			raise MacVolumeError("invalid HFS catalog node size")
		for node, offset in self._btree_records(self.catalog, node_size, first_leaf):
			length = node[offset]
			if length < 7 or offset + 1 + length > node_size:
				continue
			parent = _u32(node, offset + 2)
			name_length = node[offset + 6]
			if name_length > 31 or offset + 7 + name_length > node_size:
				continue
			name = node[offset + 7:offset + 7 + name_length].decode("mac_roman")
			record = offset + 1 + length
			if record & 1:
				record += 1
			if record + 2 > node_size:
				continue
			kind = _u16(node, record)
			if kind == 0x0100 and record + 10 <= node_size:
				self.children.setdefault(parent, []).append({
					"name": name, "folder": True,
					"id": _u32(node, record + 6)})
			elif kind == 0x0200 and record + 98 <= node_size:
				self.children.setdefault(parent, []).append({
					"name": name, "folder": False,
					"id": _u32(node, record + 20),
					"data_size": _u32(node, record + 26),
					"rsrc_size": _u32(node, record + 36),
					"data_extents": _extents16(node, record + 74, 3),
					"rsrc_extents": _extents16(node, record + 86, 3)})
		if self.extents:
			node_size = _u16(self.extents, 32)
			first_leaf = _u32(self.extents, 24)
			if node_size >= 512 and node_size % 2:
				for node, offset in self._btree_records(self.extents, node_size, first_leaf):
					length = node[offset]
					if length < 8 or offset + 1 + length > node_size:
						continue
					file_id = _u32(node, offset + 2)
					record = offset + 1 + length
					if record & 1:
						record += 1
					self.overflows.setdefault((node[offset + 1], file_id), []).extend(
						_extents16(node, record, 3))

	def fork(self, entry, resource):
		if resource:
			extents = list(entry["rsrc_extents"])
			size = entry["rsrc_size"]
			kind = 0xFF
		else:
			extents = list(entry["data_extents"])
			size = entry["data_size"]
			kind = 0
		extra = self.overflows.get((kind, entry["id"]), [])
		if extra:
			extents.extend(extra)
			extents.sort(key=lambda extent: extent[0])
		return self._fork(extents, size)


class HFSPlusVolume(_Volume):
	"""HFS+ and HFSX, which Mac OS 9 can also start from."""

	def __init__(self, stream, base):
		self.stream = stream
		self.base = base
		self.image_size = os.fstat(stream.fileno()).st_size
		self.stream.seek(base + 1024)
		header = self.stream.read(512)
		if len(header) != 512 or header[:2] not in (b"H+", b"HX"):
			raise MacVolumeError("not an HFS+ volume")
		self.block_size = _u32(header, 40)
		self.blocks_start = 0
		if self.block_size < BLOCK or self.block_size % BLOCK:
			raise MacVolumeError("invalid HFS+ allocation block size")
		catalog_size, catalog_extents = self._fork_descriptor(header, 272)
		extents_size, extents_extents = self._fork_descriptor(header, 192)
		self.catalog = self._fork(catalog_extents, catalog_size)
		self.extents = self._fork(extents_extents, extents_size)
		super().__init__(stream, base)

	@staticmethod
	def _fork_descriptor(header, offset):
		descriptor = header[offset:offset + 80]
		return _u64(descriptor, 0), _extents(descriptor, 16, 8)

	def _read_tree(self):
		if len(self.catalog) < 512:
			raise MacVolumeError("missing HFS+ catalog")
		node_size = _u16(self.catalog, 32)
		first_leaf = _u32(self.catalog, 24)
		if node_size < 512 or node_size % 2 or len(self.catalog) < node_size:
			raise MacVolumeError("invalid HFS+ catalog node size")
		for node, offset in self._btree_records(self.catalog, node_size, first_leaf):
			if offset + 8 > node_size:
				continue
			length = _u16(node, offset)
			parent = _u32(node, offset + 2)
			name_length = _u16(node, offset + 6)
			if name_length > 255 or offset + 8 + name_length * 2 > node_size:
				continue
			name = node[offset + 8:offset + 8 + name_length * 2].decode("utf-16-be")
			record = offset + 2 + length
			if record & 1:
				record += 1
			if record + 2 > node_size:
				continue
			kind = _u16(node, record)
			if kind == 0x0001 and record + 12 <= node_size:
				self.children.setdefault(parent, []).append({
					"name": name, "folder": True,
					"id": _u32(node, record + 8)})
			elif kind == 0x0002 and record + 248 <= node_size:
				self.children.setdefault(parent, []).append({
					"name": name, "folder": False,
					"id": _u32(node, record + 8),
					"data_size": _u64(node, record + 88),
					"rsrc_size": _u64(node, record + 168),
					"data_extents": _extents(node, record + 104, 8),
					"rsrc_extents": _extents(node, record + 184, 8)})
		if self.extents:
			node_size = _u16(self.extents, 32)
			first_leaf = _u32(self.extents, 24)
			if node_size >= 512 and node_size % 2:
				for node, offset in self._btree_records(self.extents, node_size, first_leaf):
					length = _u16(node, offset)
					if length < 10 or offset + 2 + length > node_size:
						continue
					file_id = _u32(node, offset + 4)
					record = offset + 2 + length
					if record & 1:
						record += 1
					self.overflows.setdefault((node[offset + 2], file_id), []).extend(
						_extents(node, record, 8))

	def fork(self, entry, resource):
		if resource:
			extents = list(entry["rsrc_extents"])
			size = entry["rsrc_size"]
			kind = 0xFF
		else:
			extents = list(entry["data_extents"])
			size = entry["data_size"]
			kind = 0
		extra = self.overflows.get((kind, entry["id"]), [])
		if extra:
			extents.extend(extra)
			extents.sort(key=lambda extent: extent[0])
		return self._fork(extents, size)


def _volume_at(stream, base):
	"""Open the volume whose master directory block starts at `base`."""
	stream.seek(base + 1024)
	header = stream.read(162)
	if len(header) < 162:
		return None
	if header[:2] == b"BD":
		# HFS+ volumes are sometimes wrapped in a small HFS volume so
		# older Macs can start from them; read the embedded volume.
		if _u16(header, 124) == 0x482B:
			block_size = _u32(header, 20)
			if block_size >= BLOCK:
				start = _u16(header, 126)
				embedded = (_u16(header, 28) + start * (block_size // BLOCK)) * BLOCK
				stream.seek(embedded + 1024)
				if stream.read(2) in (b"H+", b"HX"):
					return HFSPlusVolume(stream, embedded)
		return HFSVolume(stream, base)
	if header[:2] in (b"H+", b"HX"):
		return HFSPlusVolume(stream, base)
	return None


def open_volume(path):
	"""Return an HFS or HFS+ volume reader for `path`."""
	try:
		stream = open(path, "rb")
	except OSError as error:
		raise MacVolumeError(str(error)) from error
	try:
		stream.seek(0)
		head = stream.read(1026)
		if len(head) < 1026:
			raise MacVolumeError("image is too small to hold a Mac volume")
		volume = _volume_at(stream, 0)
		if volume:
			return volume
		if head[:2] == b"ER":
			for index in range(1, 64):
				stream.seek(index * BLOCK)
				entry = stream.read(80)
				if len(entry) != 80 or entry[:2] != b"PM":
					break
				kind = entry[48:80].split(b"\0", 1)[0]
				if kind not in (b"Apple_HFS", b"Apple_HFSX"):
					continue
				volume = _volume_at(stream, _u32(entry, 8) * BLOCK)
				if volume:
					return volume
			raise MacVolumeError("no HFS volume in the partition map")
		raise MacVolumeError("not an HFS or HFS+ volume")
	except Exception:
		stream.close()
		raise


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--image", required=True, help="the HFS or HFS+ disk image")
	parser.add_argument("--source", default="System Folder",
		help="folder to copy (default: System Folder)")
	parser.add_argument("--destination", help="where to copy it")
	parser.add_argument("--merge", action="store_true",
		help="keep existing data files and add/repair their resource forks")
	parser.add_argument("--list", action="store_true", help="list the volume instead of copying")
	args = parser.parse_args()
	try:
		volume = open_volume(args.image)
		if args.list:
			for entry in sorted(volume.children.get(2, []), key=lambda item: item["name"].casefold()):
				print(entry["name"])
			return 0
		if not args.destination:
			raise MacVolumeError("--destination is required when copying")
		volume.copy_tree(args.source, args.destination, args.merge)
		return 0
	except MacVolumeError as error:
		print("mac-hfs: " + str(error), file=sys.stderr)
		return 2


if __name__ == "__main__":
	sys.exit(main())

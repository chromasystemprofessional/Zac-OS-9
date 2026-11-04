"""A tiny classic HFS image with one file and both of its forks.

The image is built from bytes here, not copied from a Mac: it exists only
to test the reader that the Theme Installer uses for Old Dog's disk.
"""

import struct


def catalog_key(parent, name):
    raw = name.encode("mac_roman")
    return bytes([1 + 4 + 1 + len(raw), 0]) + struct.pack(">I", parent) + \
        bytes([len(raw)]) + raw


def leaf_node(records):
    node = bytearray(512)
    node[8] = 0xFF
    node[9] = 1
    node[10:12] = struct.pack(">H", len(records))
    offsets = []
    offset = 14
    for key, record in records:
        offsets.append(offset)
        node[offset:offset + len(key)] = key
        record_at = offset + len(key)
        if record_at & 1:
            record_at += 1
        node[record_at:record_at + len(record)] = record
        offset = record_at + len(record)
    for index, offset in enumerate(offsets):
        node[510 - index * 2:512 - index * 2] = struct.pack(">H", offset)
    return node


def make_hfs_image():
    """Return a 4 KiB HFS volume with System Folder/System holding DATA and
    a RSRC resource fork."""
    image = bytearray(4096)
    image[1024:1026] = b"BD"
    image[1024 + 36] = 4
    image[1024 + 37:1024 + 41] = b"Test"
    image[1024 + 18:1024 + 20] = struct.pack(">H", 8)
    image[1024 + 20:1024 + 24] = struct.pack(">I", 512)
    image[1024 + 28:1024 + 30] = struct.pack(">H", 3)
    image[1024 + 130:1024 + 134] = struct.pack(">I", 0)
    image[1024 + 146:1024 + 150] = struct.pack(">I", 1024)
    image[1024 + 150:1024 + 154] = struct.pack(">HH", 0, 2)

    header = bytearray(512)
    header[8] = 1
    header[14:16] = struct.pack(">H", 1)
    header[16:20] = struct.pack(">I", 1)
    header[20:24] = struct.pack(">I", 2)
    header[24:28] = struct.pack(">I", 1)
    header[28:32] = struct.pack(">I", 1)
    header[32:34] = struct.pack(">H", 512)
    header[34:36] = struct.pack(">H", 37)
    header[36:40] = struct.pack(">I", 2)
    header[40:44] = struct.pack(">I", 0)
    image[1536:2048] = header

    folder = struct.pack(">HHHI", 0x0100, 0, 0, 35) + bytes(60)
    data = struct.pack(">HBB", 0x0200, 2, 0) + b"zsysMACS" + bytes(8)
    data += struct.pack(">I", 2220)
    data += bytes(2) + struct.pack(">II", 4, 4)
    data += bytes(2) + struct.pack(">II", 4, 4)
    data += bytes(12 + 16 + 2)
    data += struct.pack(">HH", 2, 1) + bytes(8)
    data += struct.pack(">HH", 3, 1) + bytes(8)
    data += bytes(4)
    data += bytes(102 - len(data))
    image[2048:2560] = leaf_node([
        (catalog_key(2, "System Folder"), folder),
        (catalog_key(35, "System"), data),
    ])

    image[2560:2564] = b"DATA"
    image[3072:3076] = b"RSRC"
    return bytes(image)

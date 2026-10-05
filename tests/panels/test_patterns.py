"""Original synthetic tiles only: no Apple artwork or reference files."""
import binascii
import importlib.machinery
import importlib.util
import json
from pathlib import Path
import struct
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

tool = Path(sys.argv[1]).resolve()
loader = importlib.machinery.SourceFileLoader("user_patterns", str(tool))
spec = importlib.util.spec_from_loader(loader.name, loader)
patterns = importlib.util.module_from_spec(spec)
loader.exec_module(patterns)


def resource_file(items):
    body, refs, names = bytearray(), [], bytearray()
    kinds = sorted({item[0] for item in items})
    type_table_size = 2 + 8 * len(kinds)
    type_table, references = bytearray(struct.pack(">H", len(kinds) - 1)), bytearray()
    for kind in kinds:
        group = [item for item in items if item[0] == kind]
        type_table.extend(struct.pack(">4sHH", kind, len(group) - 1,
                                      type_table_size + len(references)))
        for _, number, name, payload in group:
            offset = len(body)
            body.extend(struct.pack(">I", len(payload)) + payload)
            name_offset = len(names)
            encoded = name.encode("mac_roman")
            names.extend(bytes([len(encoded)]) + encoded)
            references.extend(struct.pack(">hh", number, name_offset) +
                              b"\0" + offset.to_bytes(3, "big") + b"\0" * 4)
    mapping = bytearray(28) + type_table + references + names
    struct.pack_into(">HH", mapping, 24, 28, 28 + len(type_table) + len(references))
    header = struct.pack(">IIII", 256, 256 + len(body), len(body), len(mapping))
    mapping[:16] = header
    return header + b"\0" * 240 + body + mapping


def indexed_pattern():
    # A 2x2 8-bit PixMap with deliberately non-sequential palette entries.
    header = bytearray(28)
    struct.pack_into(">HII", header, 0, 1, 28, 78)
    pixmap = bytearray(50)
    struct.pack_into(">Hhhhh", pixmap, 4, 0x8002, 0, 0, 2, 2)
    struct.pack_into(">HHHH", pixmap, 30, 0, 8, 1, 8)
    struct.pack_into(">I", pixmap, 42, 82)
    palette = struct.pack(">IHH", 0, 0, 1) + struct.pack(">HHHH", 3, 65535, 0, 0)
    palette += struct.pack(">HHHH", 7, 0, 65535, 0)
    return bytes(header + pixmap) + bytes([3, 7, 7, 3]) + palette


def direct_pattern(depth):
    header = bytearray(28)
    struct.pack_into(">HII", header, 0, 1, 28, 78)
    pixmap = bytearray(50)
    struct.pack_into(">Hhhhh", pixmap, 4, 0x8000 | (depth // 8), 0, 0, 1, 1)
    struct.pack_into(">HHHH", pixmap, 30, 16, depth, 3, 5 if depth == 16 else 8)
    pixels = b"\x7c\0" if depth == 16 else b"\0\xff\0\0"
    return bytes(header + pixmap) + pixels


def hqx(data_fork, resource_fork):
    header = b"\x04Tile\0" + b"PATNTEST" + b"\0\0"
    header += struct.pack(">II", len(data_fork), len(resource_fork))
    raw = b"".join(part + struct.pack(">H", binascii.crc_hqx(part, 0))
                   for part in (header, data_fork, resource_fork))
    escaped = bytearray()
    i = 0
    while i < len(raw):
        ch = raw[i]
        end = i + 1
        while end < len(raw) and raw[end] == ch and end - i < 255:
            end += 1
        escaped.extend(b"\x90\0" if ch == 0x90 else bytes([ch]))
        if end - i > 1:
            escaped.extend(bytes([0x90, end - i]))
        i = end
    alphabet = b'!"#$%&\'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr'
    value, bits, result = 0, 0, bytearray()
    for ch in escaped:
        value = (value << 8) | ch
        bits += 8
        while bits >= 6:
            bits -= 6
            result.append(alphabet[(value >> bits) & 63])
            value &= (1 << bits) - 1
    if bits:
        result.append(alphabet[(value << (6 - bits)) & 63])
    return b"(This file must be converted with BinHex 4.0)\r:" + result + b":\r"


mono_data = b"\xaa\x55" * 4
mono_ppat = struct.pack(">HII", 0, 0, 0) + b"\0" * 10 + mono_data
color = indexed_pattern()
resource = resource_file([(b"ppat", 128, "My Tile", color),
                          (b"PAT#", 129, "Checker", b"\0\1" + mono_data)])
assert patterns.ppat(mono_ppat).getpixel((0, 0)) == (0, 0, 0)
assert patterns.ppat(color).getpixel((0, 0)) == (255, 0, 0)
assert patterns.ppat(color).getpixel((1, 0)) == (0, 255, 0)
assert patterns.ppat(direct_pattern(16)).getpixel((0, 0)) == (255, 0, 0)
assert patterns.ppat(direct_pattern(32)).getpixel((0, 0)) == (255, 0, 0)
assert len(patterns.resources(resource)) == 2
collection = struct.pack(">HI", 1, 6) + color
assert len(patterns.resources(resource_file([(b"ppt#", 10, "Collection", collection)]))) == 1
assert len(patterns.resources(resource_file([(b"PAT ", 10, "Mono", mono_data)]))) == 1
assert patterns.binhex(hqx(b"", resource)) == (b"", resource)
assert patterns.binhex(hqx(b"\x90" * 32, b"")) == (b"\x90" * 32, b"")
try:
    patterns.binhex(hqx(b"data", resource).replace(b"!", b'"', 1))
except ValueError:
    pass
else:
    raise AssertionError("BinHex checksum corruption was not rejected")

with tempfile.TemporaryDirectory(prefix="zacos9-pattern-test-") as directory:
    root = Path(directory)
    Image.new("RGB", (3, 2), (12, 34, 56)).save(root / "User.png")
    Image.new("RGB", (2, 2), "blue").save(root / "User.jpg")
    (root / "Mono.ppat").write_bytes(mono_ppat)
    (root / "Color.ppat").write_bytes(color)
    (root / "My Patterns").write_bytes(resource)
    (root / "BinHex.hqx").write_bytes(hqx(b"", resource))
    header = bytearray(128)
    header[1:6] = b"\x04Tile"
    struct.pack_into(">II", header, 83, 3, len(resource))
    (root / "MacBinary.bin").write_bytes(bytes(header) + b"abc" + b"\0" * 125 + resource)
    apple_double = struct.pack(">II16sH", 0x00051607, 0x00020000, b"\0" * 16, 1)
    apple_double += struct.pack(">III", 2, 38, len(resource)) + resource
    (root / "Sidecar").write_bytes(b"")
    (root / "._Sidecar").write_bytes(apple_double)
    # Original PICT v1: a black painted rectangle, 4x4 pixels.
    picture = b"\0\0" + struct.pack(">hhhh", 0, 0, 4, 4) + b"\x11\x01\x31"
    picture += struct.pack(">hhhh", 0, 0, 4, 4) + b"\xff"
    (root / "User.pict").write_bytes(b"\0" * 512 + picture)
    (root / "Raw.pict").write_bytes(picture)
    converter = shutil.which("magick") or shutil.which("convert")
    assert converter, "Install imagemagick to validate PICT"
    subprocess.run([converter, str(root / "User.png"), "PICT:" + str(root / "Raster.pict")],
                   check=True, capture_output=True, timeout=10)
    assert patterns.read_file(root / "Raster.pict")[0][2].convert("RGB").getpixel((0, 0)) == (12, 34, 56)
    (root / "Picture Resources").write_bytes(resource_file([(b"PICT", 42, "Rectangle", picture)]))
    (root / "BinHex Picture.hqx").write_bytes(hqx(picture, b""))
    picture_fork = b"\0" * 512 + picture
    header = bytearray(128)
    header[1:6] = b"\x04Tile"
    struct.pack_into(">II", header, 83, len(picture_fork), 0)
    (root / "MacBinary Picture.bin").write_bytes(bytes(header) + picture_fork)
    (root / "Bad.ppat").write_bytes(b"\0")
    (root / "Bad.hqx").write_bytes(b":bad:")
    (root / "Bad.pict").write_bytes(b"not a picture")
    result = subprocess.run([sys.executable, str(tool), str(root)],
                            capture_output=True, text=True, timeout=30, check=True)
    catalog = json.loads(result.stdout)
    assert len(catalog["entries"]) == 18, catalog
    assert len(catalog["errors"]) == 3, catalog["errors"]
    ids = [entry["id"] for entry in catalog["entries"]]
    assert len(set(ids)) == len(ids)
    assert any("My Tile" in entry["name"] for entry in catalog["entries"])
    assert not any(error.startswith("Sidecar:") for error in catalog["errors"])
    before = (root / "User.png").read_bytes()
    again = patterns.scan(root)
    assert [entry["id"] for entry in again["entries"]] == ids
    assert (root / "User.png").read_bytes() == before, "Import must not change source files"
    (root / "User.png").unlink()
    assert len(patterns.scan(root)["entries"]) == 17
    bad_offset = bytearray(resource)
    struct.pack_into(">I", bad_offset, 0, 0xffffffff)
    try:
        patterns.resources(bad_offset)
    except ValueError:
        pass
    else:
        raise AssertionError("Invalid resource offsets accepted")
    for data in (b"", color[:40], color[:-1]):
        try:
            patterns.ppat(data)
        except ValueError:
            pass
        else:
            raise AssertionError("Truncated pattern accepted")
    wallpaper = root / "wallpaper"
    wallpaper.mkdir()
    Image.new("RGB", (1500, 800), (10, 20, 30)).save(wallpaper / "Photo.png")
    (wallpaper / "Picture.pict").write_bytes(b"\0" * 512 + picture)
    (wallpaper / "BinHex.hqx").write_bytes(hqx(b"", resource_file([(b"PICT", 42, "Photo", picture)])))
    (wallpaper / "Wrong Folder.ppat").write_bytes(color)
    result = subprocess.run([sys.executable, str(tool), "--wallpaper", str(wallpaper)],
                            capture_output=True, text=True, timeout=30, check=True)
    photos = json.loads(result.stdout)
    assert len(photos["entries"]) == 3, photos
    assert all(entry["id"].startswith("wallpaper:") for entry in photos["entries"])
    assert len(photos["errors"]) == 1 and "pattern tiles belong" in photos["errors"][0]
    result = subprocess.run([sys.executable, str(tool), str(wallpaper)],
                            capture_output=True, text=True, timeout=30, check=True)
    small_tiles = json.loads(result.stdout)
    assert not any(entry["name"].startswith("Photo.png") for entry in small_tiles["entries"])
    assert any(error.startswith("Photo.png:") and ("limit" in error or "dimensions" in error)
               for error in small_tiles["errors"])
print("ok: original image, ppat, PAT#, PICT, resource, MacBinary, AppleDouble and BinHex fixtures")
print("ok: IDs, palette colors, checksums, corrupt inputs, removal and source preservation")
print("ok: separate wallpaper catalog, photo-size limits and classic PICT pictures")

import importlib.util
import struct
import sys

spec = importlib.util.spec_from_file_location("macpict", sys.argv[1])
macpict = importlib.util.module_from_spec(spec)
spec.loader.exec_module(macpict)


def test_packbits():
    # Literal run of 3 bytes: 0x02 0xAA 0xBB 0xCC
    assert macpict.packbits_decode(b"\x02\xaa\xbb\xcc", 3) == b"\xaa\xbb\xcc"
    # Repeat: 0x80 -> 1 byte, 0xFE -> 3 bytes
    assert macpict.packbits_decode(b"\xfe\x55", 3) == b"\x55\x55\x55"
    # Mixed: literal 'ab' then repeat 'c' x3
    d = b"\x01ab" + b"\xfd" + b"c"
    assert macpict.packbits_decode(d, 5) == b"abccc"
    print("ok    packbits")


def _pict(width, height, op, body):
    header = bytearray(512)
    struct.pack_into(">hhhh", header, 2, 0, 0, height, width)
    return bytes(header) + struct.pack(">H", op) + body + struct.pack(">H", 0x00FF)


def test_bitsrect():
    # 8x1 1-bit image, one byte 0x80 -> first pixel black.
    body = struct.pack(">H", 1) + struct.pack(">hhhh", 0, 0, 1, 8) + b"\x80"
    w, h, rows = macpict.decode_pict(_pict(8, 1, 0x0090, body))
    assert (w, h) == (8, 1)
    assert rows[0][0] == (0, 0, 0, 255)
    assert rows[0][1] == (255, 255, 255, 255)
    print("ok    bitsrect")


def test_packbitsrect():
    # 8x1 1-bit image, one byte 0xFF -> all black.
    body = struct.pack(">H", 1) + struct.pack(">hhhh", 0, 0, 1, 8) + b"\x00\xff"
    w, h, rows = macpict.decode_pict(_pict(8, 1, 0x0098, body))
    assert (w, h) == (8, 1)
    assert all(px == (0, 0, 0, 255) for px in rows[0])
    print("ok    packbitsrect")


def test_pixmap_24bit():
    # 2x1 24-bit direct pixmap.
    body = bytearray()
    body += struct.pack(">I", 0)  # baseAddr
    body += struct.pack(">H", 0x8000 | 6)  # rowBytes, pixmap flag
    body += struct.pack(">hhhh", 0, 0, 1, 2)  # bounds
    body += struct.pack(">H", 0)  # pmVersion
    body += struct.pack(">H", 0)  # packType (unpacked)
    body += struct.pack(">I", 0)  # packSize
    body += struct.pack(">II", 0, 0)  # hRes, vRes
    body += struct.pack(">H", 0)  # pixelType (chunky)
    body += struct.pack(">H", 24)  # pixelSize
    body += struct.pack(">H", 3)  # cmpCount
    body += struct.pack(">H", 8)  # cmpSize
    body += struct.pack(">I", 0)  # planeBytes
    body += struct.pack(">I", 0)  # pmTable
    body += struct.pack(">I", 0)  # pmReserved
    body += bytes((255, 0, 0, 0, 0, 255))  # two pixels: red, blue
    rows = macpict.decode_pixmap(bytes(body))
    assert len(rows) == 1 and len(rows[0]) == 2
    assert rows[0][0] == (255, 0, 0, 255)
    assert rows[0][1] == (0, 0, 255, 255)
    print("ok    pixmap 24-bit")


test_packbits()
test_bitsrect()
test_packbitsrect()
test_pixmap_24bit()
print("OK")

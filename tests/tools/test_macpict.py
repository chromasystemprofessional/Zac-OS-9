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


test_packbits()
test_bitsrect()
test_packbitsrect()
print("OK")

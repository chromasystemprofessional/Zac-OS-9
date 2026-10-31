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
    """A version 1 PICT resource: size, bounds, version opcode, one bitmap
    opcode with its body, then end-of-picture."""
    return (struct.pack(">H", 0) + struct.pack(">hhhh", 0, 0, height, width)
            + b"\x11\x01" + bytes((op,)) + body + b"\xff")


def _bitmap_body(rowbytes, width, height, pixels):
    """BitsRect body: row bytes, bounds, source and destination rects, mode."""
    return (struct.pack(">H", rowbytes) + struct.pack(">hhhh", 0, 0, height, width)
            + struct.pack(">hhhh", 0, 0, height, width) * 2 + struct.pack(">H", 0) + pixels)


def test_bitsrect():
    # 8x1 1-bit image, one byte 0x80 -> first pixel black.
    body = _bitmap_body(1, 8, 1, b"\x80")
    w, h, rows = macpict.decode_pict(_pict(8, 1, 0x0090, body))
    assert (w, h) == (8, 1)
    assert rows[0][0] == (0, 0, 0, 255)
    assert rows[0][1] == (255, 255, 255, 255)
    print("ok    bitsrect")


def test_packbitsrect():
    # 8x1 1-bit image, one byte 0xFF -> all black.
    # Rows narrower than 8 bytes are never packed: 0xFF is eight black pixels.
    body = _bitmap_body(1, 8, 1, b"\xff")
    w, h, rows = macpict.decode_pict(_pict(8, 1, 0x0098, body))
    assert (w, h) == (8, 1)
    assert all(px == (0, 0, 0, 255) for px in rows[0])
    # A packed 8-byte row: count 2, then 0xF9 repeats 0xFF eight times.
    packed = _bitmap_body(8, 64, 1, bytes((2, 0xF9, 0xFF)))
    w, h, rows = macpict.decode_pict(_pict(64, 1, 0x0098, packed))
    assert (w, h) == (64, 1)
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


def test_packbits_16bit_units():
    # Runs count 16-bit pixels: 0x81 repeats the next pixel 128 times, and
    # 0xCE repeats the one after it 51 times (358 bytes, one 179-pixel row).
    data = bytes((0x81, 0x00, 0x00, 0xCE, 0x00, 0x00))
    out = macpict.packbits_decode(data, 358, unit=2)
    assert len(out) == 358 and out == bytes(358), len(out)
    lit = macpict.packbits_decode(bytes((0x01, 0x12, 0x34, 0x56, 0x78)), 4, unit=2)
    assert lit == b"\x12\x34\x56\x78", lit
    print("ok    16-bit packbits units")


test_packbits_16bit_units()
print("OK")

import importlib.util
import sys

spec = importlib.util.spec_from_file_location("macpat", sys.argv[1])
macpat = importlib.util.module_from_spec(spec)
spec.loader.exec_module(macpat)


def test_pat():
    data = bytes([0x80]) + bytes([0x00]) * 7
    rows = macpat.decode_pat(data)
    assert len(rows) == 8 and len(rows[0]) == 8
    assert rows[0][0] == (0, 0, 0, 255)
    assert rows[0][1] == (255, 255, 255, 255)
    assert rows[1][0] == (255, 255, 255, 255)
    print("ok    pat")


def test_rgba():
    rows = [[(1, 2, 3, 4)] * 8 for _ in range(8)]
    out = macpat.rgba_bytes(rows)
    assert len(out) == 8 * 8 * 4
    assert out[:4] == bytes((1, 2, 3, 4))
    print("ok    rgba")


def test_ppat_colour():
    import struct
    pixmap_at, table_at, pixels_at = 28, 78, 102
    header = struct.pack(">HIIIHIII", 1, pixmap_at, pixels_at, 0, 0xFFFF, 0, 0, 0)
    pixmap = struct.pack(">IHhhhhHHIIIHHHHIII", 0, 0x8008, 0, 0, 8, 8, 0, 0, 0,
                         0x480000, 0x480000, 0, 8, 1, 8, 0, table_at, 0)
    table = struct.pack(">IHH", 0, 0, 1) + struct.pack(">HHHH", 0, 0xFFFF, 0xFFFF, 0xFFFF) \
        + struct.pack(">HHHH", 1, 0, 0, 0)
    pixels = b"\x01" * 64
    width, height, rows = macpat.decode_ppat(header[:28] + pixmap + table + pixels)
    assert (width, height) == (8, 8), (width, height)
    assert rows[0][0] == (0, 0, 0, 255), rows[0][0]
    print("ok    colour ppat")


def test_ppat_old_style():
    data = bytearray(28)
    data[1] = 0
    data[20:28] = bytes([0x80]) + bytes(7)
    width, height, rows = macpat.decode_ppat(bytes(data))
    assert (width, height) == (8, 8)
    assert rows[0][0] == (0, 0, 0, 255) and rows[0][1] == (255, 255, 255, 255)
    print("ok    old-style ppat")


test_pat()
test_rgba()
test_ppat_colour()
test_ppat_old_style()
print("OK")


def _ppat():
    # A minimal 8x8 8-bit ppat: checkerboard of black and white.
    import struct as _s
    pixmap = _s.pack(">H4HHHI", 0x8008, 0, 0, 8, 8, 0, 0, 0)
    pixmap += _s.pack(">IIHHHHIII", 0x00480000, 0x00480000, 0, 8, 1, 8, 0, 142, 0)
    assert len(pixmap) == 46
    pixels = bytes((x + y) % 2 for y in range(8) for x in range(8))
    clut = _s.pack(">IHH", 0, 0, 1) + _s.pack(">HHHH", 0, 0, 0, 0) + \
        _s.pack(">HHHH", 1, 0xFFFF, 0xFFFF, 0xFFFF)
    assert len(clut) == 8 + 16
    return _s.pack(">H", 1) + _s.pack(">I", 28) + _s.pack(">I", 78) + b"\x00" * 10 + \
        b"\x00" * 8 + b"\x00" * 4 + pixmap + pixels + clut


def test_ppat():
    width, height, rows = macpat.decode_ppat(_ppat())
    assert (width, height) == (8, 8) and len(rows) == 8 and len(rows[0]) == 8, "8x8 tile decoded"
    assert rows[0][0] == (0, 0, 0, 255) and rows[0][1] == (255, 255, 255, 255), \
        "indexed pixels use the colour table"
    assert rows[1][0] == (255, 255, 255, 255), "checkerboard alternates"
    for bad in (b"", b"\x00" * 10):
        try:
            macpat.decode_ppat(bad)
            assert False, "junk must fail"
        except ValueError:
            pass
    print("ok    ppat")


test_pat()
test_rgba()
test_ppat()
print("OK")

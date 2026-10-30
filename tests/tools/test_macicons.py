import importlib.util
import os
import sys

spec = importlib.util.spec_from_file_location("macicons", sys.argv[1])
macicons = importlib.util.module_from_spec(spec)
spec.loader.exec_module(macicons)


def test_clut_lookup():
    clut = [(1, (255, 0, 0)), (0, (0, 255, 0)), (2, (0, 0, 255))]
    assert macicons.clut_lookup(clut) == [(0, 255, 0), (255, 0, 0), (0, 0, 255)]
    print("ok    clut lookup")


def test_icl8():
    clut = [(i, (i, i, i)) for i in range(256)]
    data = bytes(range(256)) * 4  # 32x32 = 1024 bytes, first row 0..255
    rows = macicons.decode_icl8(data, clut, size=32)
    assert len(rows) == 32 and len(rows[0]) == 32
    assert rows[0][0] == (0, 0, 0, 255)
    assert rows[0][5] == (5, 5, 5, 255)
    print("ok    icl8")


def test_icl4():
    clut = [(i, (i * 16, i * 16, i * 16)) for i in range(16)]
    data = bytearray()
    for _ in range(32 * 16):
        data += bytes((0x3F,))  # left=3, right=15
    rows = macicons.decode_icl4(bytes(data), clut, size=32)
    assert rows[0][0] == (48, 48, 48, 255)  # nibble 3 -> 3*16
    assert rows[0][1] == (240, 240, 240, 255)  # nibble 15 -> 240
    print("ok    icl4")


def test_icn1():
    # 32x32: a 1-bit bitmap then mask. Set every bit.
    data = b"\xff" * 128 + b"\xff" * 128
    bitmap, mask = macicons.decode_icn1(data, size=32)
    assert all(bitmap[0]) and all(mask[0])
    # Second half mask cleared -> transparent.
    data2 = b"\xff" * 128 + b"\x00" * 128
    _, mask2 = macicons.decode_icn1(data2, size=32)
    assert not any(mask2[0])
    print("ok    icn1")


def test_combine():
    color = [[(10, 20, 30, 255)] * 32 for _ in range(32)]
    mask = [[True] * 32 for _ in range(32)]
    mask[0][0] = False
    rows = macicons.combine_icon(color, mask)
    assert rows[0][0] == (0, 0, 0, 0)
    assert rows[0][1] == (10, 20, 30, 255)
    print("ok    combine")


test_clut_lookup()
test_icl8()
test_icl4()
test_icn1()
test_combine()
print("OK")

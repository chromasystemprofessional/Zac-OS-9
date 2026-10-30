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


test_pat()
test_rgba()
print("OK")

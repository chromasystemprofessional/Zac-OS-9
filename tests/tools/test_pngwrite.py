import importlib.util
import struct
import sys
import zlib

spec = importlib.util.spec_from_file_location("pngwrite", sys.argv[1])
pngwrite = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pngwrite)


def test_write_png_roundtrip():
    import tempfile, os
    rows = [[(255, 0, 0, 255), (0, 255, 0, 255)],
            [(0, 0, 255, 255), (255, 255, 255, 0)]]
    path = tempfile.mktemp(suffix=".png")
    pngwrite.write_png(path, 2, 2, rows)
    data = open(path, "rb").read()
    os.unlink(path)
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    # IHDR: width=2, height=2, bit depth 8, color type 6 (RGBA)
    assert struct.unpack(">IIBBBBB", data[16:29]) == (2, 2, 8, 6, 0, 0, 0)
    print("ok    write_png")


def test_nearest_scale():
    rows = [[(10, 10, 10, 255), (20, 20, 20, 255)],
            [(30, 30, 30, 255), (40, 40, 40, 255)]]
    out = pngwrite.nearest_scale(rows, 2, 2, 4, 4)
    assert len(out) == 4 and len(out[0]) == 4
    assert out[0][0] == (10, 10, 10, 255)
    assert out[3][3] == (40, 40, 40, 255)
    print("ok    nearest_scale")


def test_fit_scale():
    rows = [[(1, 2, 3, 255)] * 4 for _ in range(4)]
    out = pngwrite.fit_scale(rows, 4, 4, 20, 15)
    assert len(out) == 15 and len(out[0]) == 20
    # centred: top-left corner is transparent padding
    assert out[0][0] == (0, 0, 0, 0)
    assert out[7][9] == (1, 2, 3, 255)
    print("ok    fit_scale")


test_write_png_roundtrip()
test_nearest_scale()
test_fit_scale()
print("OK")

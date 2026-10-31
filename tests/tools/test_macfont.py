import importlib.util
import struct
import sys

spec = importlib.util.spec_from_file_location("macfont", sys.argv[1])
macfont = importlib.util.module_from_spec(spec)
spec.loader.exec_module(macfont)


def strike(first, last, frh, row_words, ascent, image, loc, ow):
    """An NFNT strike: header, bitmap image, location table, offset/width table."""
    owt = (26 + len(image) + 2 * len(loc) - 16) // 2
    head = struct.pack(">13H", 0, first, last, 1, 0, 0, 1, frh, owt, ascent, 0, 0, row_words)
    return head + image + struct.pack(">%dH" % len(loc), *loc) + struct.pack(">%dH" % len(ow), *ow)


def test_strike_glyph():
    # One glyph (code 65) one pixel wide, two rows tall, with ink in column 0.
    image = struct.pack(">HH", 0x8000, 0x8000)
    loc = [0, 1, 2]
    ow = [0x0000, 0xFFFF, 0xFFFF]
    glyphs = macfont.strike_glyphs(strike(65, 65, 2, 1, 1, image, loc, ow))
    advance, top, rows = glyphs[65]
    assert (advance, top) == (1, -1), (advance, top)
    assert rows == [[1], [1]], rows
    assert set(glyphs) == {65}, glyphs.keys()
    print("ok    strike glyph")


test_strike_glyph()
print("OK")

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
    # One glyph (code 65): 1 px of ink, advance 3, no bearing; space (code 32)
    # has no ink and advance 4; code 66 is missing (ow 0xFFFF).
    image = struct.pack(">HH", 0x8000, 0x8000)
    loc = [0] * 34 + [1, 1, 1]
    ow = [0x0004] + [0xFFFF] * 32 + [0x0003] + [0xFFFF] * 3
    glyphs = macfont.strike_glyphs(strike(32, 66, 2, 1, 1, image, loc, ow))
    advance, top, rows = glyphs[65]
    assert (advance, top) == (3, -1), (advance, top)
    assert rows == [[1, 0, 0], [1, 0, 0]], rows
    assert glyphs[32] == (4, -1, [[0, 0, 0, 0], [0, 0, 0, 0]]), glyphs[32]
    assert set(glyphs) == {32, 65}, glyphs.keys()
    print("ok    strike glyph")


def test_strike_bearing():
    # Bearing 1: ink sits one column right of the pen, advance 4.
    image = struct.pack(">HH", 0x8000, 0x8000)
    glyphs = macfont.strike_glyphs(strike(65, 65, 2, 1, 1, image, [0, 1, 2], [0x0104, 0xFFFF, 0xFFFF]))
    assert glyphs[65][0] == 4, glyphs[65]
    assert glyphs[65][2] == [[0, 1, 0, 0], [0, 1, 0, 0]], glyphs[65][2]
    print("ok    strike bearing")


def test_pfont_text():
    image = struct.pack(">HH", 0x8000, 0x8000)
    text = macfont.pfont_text("Test", 12, strike(65, 65, 2, 1, 1, image, [0, 1, 2], [0x0003, 0xFFFF, 0xFFFF]))
    assert "font Test\nheight 2\nbaseline 1" in text, text
    assert "char U+0041 3 -1\n#..\n#..\n" in text, text
    print("ok    pfont text")


test_strike_glyph()
test_strike_bearing()
test_pfont_text()
print("OK")

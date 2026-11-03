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


def _name_table(names):
    # names: [(platform, script, lang, name_id, str)] -> name table bytes.
    records = b""
    strings = b""
    encoded = []
    for platform, script, lang, name_id, text in names:
        raw = text.encode("utf-16-be") if platform != 1 else text.encode("mac_roman")
        encoded.append((platform, script, lang, name_id, raw))
    off = 0
    for platform, script, lang, name_id, raw in encoded:
        records += struct.pack(">HHHHHH", platform, script, lang, name_id, len(raw), off)
        strings += raw
        off += len(raw)
    return struct.pack(">HHH", 0, len(encoded), 6 + 12 * len(encoded)) + records + strings


def _head_table():
    return struct.pack(">II", 0x00010000, 0) + struct.pack(">I", 0) + \
        struct.pack(">IHH", 0x5F0F3CF5, 0, 2048) + b"\x00" * 16 + \
        struct.pack(">hhhhhhhhh", 0, 0, 0, 0, 0, 8, 2, 0, 0)


def _assemble(tables):
    # tables: [(tag, bytes)] -> a valid sfnt with correct offsets/checksum.
    out = bytearray(struct.pack(">IHHHH", 0x00010000, len(tables), 0, 0, 0))
    power, step = 1, 0
    while power * 2 <= len(tables):
        power *= 2
        step += 1
    out[6:8] = struct.pack(">H", power * 16)
    out[8:10] = struct.pack(">H", step)
    out[10:12] = struct.pack(">H", len(tables) * 16 - power * 16)
    pos = 12 + 16 * len(tables)
    directory = bytearray()
    blob = bytearray()
    for tag, body in tables:
        while pos % 4:
            pos += 1
            blob.append(0)
        directory += struct.pack(">4sIII", tag, 0, pos, len(body))
        blob += body
        pos += len(body)
    out += directory + blob
    # checkSumAdjustment over the zeroed file.
    at = 12 + 16 * len(tables)
    for tag, body in tables:
        while at % 4:
            at += 1
        if tag == b"head":
            break
        at += len(body)
    else:
        raise ValueError("no head table")
    for i in range(at + 8, at + 12):
        out[i] = 0
    total = 0
    padded = bytes(out) + b"\x00" * (-len(out) % 4)
    for i in range(0, len(padded), 4):
        total = (total + struct.unpack(">I", padded[i:i + 4])[0]) & 0xFFFFFFFF
    out[at + 8:at + 12] = struct.pack(">I", (0xB1B0AFBA - total) & 0xFFFFFFFF)
    return bytes(out)


def _sfnt(names):
    # names: [(platform, script, lang, name_id, str)] -> minimal sfnt bytes.
    table = _name_table(names)
    table += b"\x00" * (-len(table) % 4)
    return _assemble([(b"head", _head_table()), (b"name", table)])


def test_is_sfnt():
    assert macfont.is_sfnt(b"\x00\x01\x00\x00" + b"\x00" * 8)
    assert macfont.is_sfnt(b"OTTO" + b"\x00" * 8)
    assert not macfont.is_sfnt(b"not a font!!")
    assert not macfont.is_sfnt(b"short")
    print("ok    sfnt detection")


def test_names_unicode():
    data = _sfnt([(3, 1, 0x409, 1, "Charcoal"), (3, 1, 0x409, 2, "Regular")])
    assert macfont.parse_sfnt_names(data) == ("Charcoal", "Regular")
    data = _sfnt([(3, 1, 0x409, 1, "Arial"), (3, 1, 0x409, 2, "Bold")])
    assert macfont.parse_sfnt_names(data) == ("Arial", "Bold")
    print("ok    unicode family and style names")


def test_names_mac_roman_fallback():
    data = _sfnt([(1, 0, 0, 1, "Geneva"), (1, 0, 0, 2, "Regular")])
    assert macfont.parse_sfnt_names(data) == ("Geneva", "Regular")
    print("ok    mac roman fallback names")


def test_names_missing():
    try:
        macfont.parse_sfnt_names(_assemble([(b"head", _head_table())]))
        assert False, "a font without names must fail"
    except ValueError:
        print("ok    nameless sfnt refused")


def test_filenames():
    taken = set()
    assert macfont.ttf_filename("Charcoal", "Regular", taken) == "Charcoal.ttf"
    assert macfont.ttf_filename("Charcoal", "Bold", taken) == "Charcoal-Bold.ttf"
    assert macfont.ttf_filename("Charcoal", "Bold", taken) == "Charcoal-Bold_2.ttf"
    assert macfont.ttf_filename("Weird/Name", "Regular", taken) == "Weird_Name.ttf"
    print("ok    face filenames are safe and unique")


def test_sanitize_drops_aat():
    table = _name_table([(3, 1, 0x409, 1, "Charcoal"), (3, 1, 0x409, 2, "Regular")])
    data = _assemble([(b"head", _head_table()), (b"name", table), (b"mort", b"\x00" * 32)])
    assert macfont.is_sfnt(data)
    clean = macfont.sanitize_sfnt(data)
    assert macfont.is_sfnt(clean)
    assert macfont.parse_sfnt_names(clean) == ("Charcoal", "Regular"), "names survive"
    num = struct.unpack(">H", clean[4:6])[0]
    tags = [clean[12 + 16 * i:12 + 16 * i + 4] for i in range(num)]
    assert b"mort" not in tags and b"morx" not in tags, "AAT layout tables are gone"
    assert b"head" in tags and b"name" in tags, "real tables stay"
    total = 0
    padded = clean + b"\x00" * (-len(clean) % 4)
    for i in range(0, len(padded), 4):
        total = (total + struct.unpack(">I", padded[i:i + 4])[0]) & 0xFFFFFFFF
    assert total == 0xB1B0AFBA, "checksum is valid"
    assert macfont.sanitize_sfnt(clean) == clean, "sanitizing is idempotent"
    print("ok    AAT tables stripped, checksum fixed")


def test_sanitize_rejects_junk():
    for bad in (b"", b"short", b"\x00" * 100):
        try:
            macfont.sanitize_sfnt(bad)
            assert False, "junk must fail"
        except ValueError:
            pass
    print("ok    junk sfnt refused")


def test_rasterize_pfont():
    import os as _os
    candidates = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu-core/DejaVuSans.ttf",
    ]
    ttf = next((p for p in candidates if _os.path.isfile(p)), None)
    assert ttf is not None, "DejaVu Sans must be installed (fonts-dejavu-core)"
    first = macfont.rasterize_pfont(ttf, 12)
    assert macfont.rasterize_pfont(ttf, 12) == first, "raster output is deterministic"
    lines = first.split("\n")
    assert lines[0].startswith("font "), "header names the face"
    meta = {}
    for line in lines[1:5]:
        key, value = line.split(" ", 1)
        meta[key] = int(value)
    assert meta["height"] > 0 and 0 < meta["baseline"] < meta["height"], meta
    # Every data row is exactly its glyph's advance wide: the contract
    # tools/fonts/build_fonts.py checks, and what the C reader expects.
    advance, bad = 0, 0
    glyphs = 0
    for line in lines:
        if line.startswith("char "):
            advance = int(line.split(" ")[-2])
            glyphs += 1
        elif line and set(line) <= set("#."):
            if len(line) != advance:
                bad += 1
    assert glyphs > 100, f"a text face covers Latin-1: {glyphs}"
    assert bad == 0, f"{bad} rows are not their advance wide"
    assert "\nchar space " in first, "space stays blank (fallback keeps its shape)"
    assert "\nchar A " in first, "basic Latin is covered"
    # The file loads in the same compiler as the built-ins, so what the
    # extractor writes is what the desktop can read.
    import importlib.util as _iu
    root = _os.path.dirname(sys.argv[1])
    spec = _iu.spec_from_file_location("build_fonts",
        _os.path.join(root, "fonts", "build_fonts.py"))
    bf = _iu.module_from_spec(spec)
    spec.loader.exec_module(bf)
    import tempfile as _tf
    with _tf.NamedTemporaryFile("w", suffix=".pfont", delete=False) as tmp:
        tmp.write(first)
        path = tmp.name
    try:
        font = bf.Font(path)
    finally:
        _os.unlink(path)
    assert len(font.glyphs) > 100, "compiled raster keeps its glyphs"
    try:
        macfont.rasterize_pfont("/no/such/font.ttf", 12)
        assert False, "a missing face must fail"
    except ValueError:
        pass
    print("ok    faces rasterize to valid .pfont bitmap fonts")


test_is_sfnt()
test_names_unicode()
test_names_mac_roman_fallback()
test_names_missing()
test_filenames()
test_sanitize_drops_aat()
test_sanitize_rejects_junk()
test_rasterize_pfont()
print("OK")

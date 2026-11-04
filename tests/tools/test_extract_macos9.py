import importlib.util
import os
import struct
import subprocess
import sys
import tempfile

# Load the tool as a module to test parse_resource_fork directly.
spec = importlib.util.spec_from_file_location("extract", sys.argv[1])
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


def build_resource_fork(resources):
    """Build a valid resource fork from [(type, id, body)].

    Header (data_off, map_off, data_len, map_len), then the data section
    (each resource prefixed by a 4-byte length), then the resource map whose
    reference entries carry each resource's real data offset."""
    data_offset = 256
    data = b""
    offsets = []
    for rtype, res_id, body in resources:
        offsets.append((rtype, res_id, len(data)))
        data += struct.pack(">I", len(body)) + body

    from collections import OrderedDict
    type_counts = OrderedDict()
    for rtype, res_id, body in resources:
        type_counts.setdefault(rtype, 0)
        type_counts[rtype] += 1

    # Type list: num types, then per type: type(4), num-1(2), ref-list offset(2)
    # (offset relative to the start of the type list).
    header_size = 2 + 8 * len(type_counts)
    cursor = header_size
    type_list = struct.pack(">H", len(type_counts) - 1)
    for rtype, count in type_counts.items():
        type_list += struct.pack(">4sHH", rtype, count - 1, cursor)
        cursor += 12 * count

    # Reference lists (no leading count: each is `count` 12-byte entries).
    for rtype, count in type_counts.items():
        for rt, rid, off in offsets:
            if rt != rtype:
                continue
            type_list += struct.pack(">h", rid)
            type_list += struct.pack(">H", 0xFFFF)  # no name
            type_list += bytes([0])                 # attributes
            type_list += struct.pack(">I", off)[1:] # 3-byte data offset
            type_list += struct.pack(">I", 0)       # reserved

    map_size = 28 + len(type_list)
    name_list_offset = 28 + len(type_list)
    map_header = struct.pack(">IIII", data_offset, data_offset + len(data), len(data), map_size)
    map_info = struct.pack(">IHH", 0, 0, 0)       # next map, file ref, attrs
    map_ = map_header + map_info + struct.pack(">HH", 28, name_list_offset) + type_list

    return struct.pack(">IIII", data_offset, data_offset + len(data), len(data), map_size) \
        + b"\x00" * (data_offset - 16) + data + map_


def test_parse():
    fork = build_resource_fork([(b"ICN#", 128, b"\x01\x02\x03")])
    items = tool.parse_resource_fork(fork)
    assert len(items) == 1, f"expected 1 resource, got {len(items)}"
    assert items[0][0] == b"ICN#", "type mismatch"
    assert items[0][2] == b"\x01\x02\x03", "body mismatch"
    print("ok    synthetic resource fork parsed")


def test_end_to_end():
    src = tempfile.mkdtemp()
    with open(os.path.join(src, "a.png"), "wb") as f:
        f.write(b"png")
    # Build a resource fork and save as a file
    fork = build_resource_fork([(b"icns", 128, b"iconbytes"), (b"thme", 1, b"thmedata")])
    with open(os.path.join(src, "Finder"), "wb") as f:
        f.write(fork)
    out = tempfile.mkdtemp() + "/theme"
    r = subprocess.run(
        [sys.executable, sys.argv[1], "--source", src, "--output", out, "--name", "T"],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    assert os.path.isfile(os.path.join(out, "theme.conf"))
    assert os.path.isfile(os.path.join(out, "icons", "a.png"))
    assert any(fn.startswith("icns_") for fn in os.listdir(os.path.join(out, "icons"))), \
        "resource-fork icon extracted"
    thme_files = [fn for fn in os.listdir(os.path.join(out, "theme")) if fn.startswith("thme_")]
    assert thme_files, "resource-fork theme extracted"
    with open(os.path.join(out, "theme", thme_files[0]), "rb") as f:
        assert f.read() == b"thmedata", "theme body is the full resource"
    print("ok    end-to-end bundle build")


def build_scen(items):
    """Build a flattened Appearance collection from [(tag, value_bytes)]."""
    import struct as _s
    header = _s.pack(">III", 0x00010000, 0x40000000, len(items))
    headers = b""
    pool = b""
    offs = []
    for tag, value in items:
        offs.append(len(pool))
        headers += _s.pack(">4sIII", tag, 0, 0x40000000, len(pool))
        pool += value
    return header + headers + _s.pack(">I", len(pool)) + pool


def test_scen_patt_epic():
    # scen with a patt blob (desktop pattern), an epic id and a dpnm name.
    import struct as _s
    patt = bytes(28)  # type-1 header stub; decode failure is fine, capture matters
    scen = build_scen([(b"patt", patt), (b"epic", _s.pack(">h", 2011)),
                       (b"dpnm", bytes((5,)) + b"Beach"),
                       (b"sndt", bytes((5,)) + b"Waves")])
    # A minimal version-1 BitsRect PICT (kept: not epic, not splash;
    # at 200x160 it also clears the icon-sized-PICT filter).
    pict = (_s.pack(">H", 0) + _s.pack(">hhhh", 0, 0, 160, 200) + b"\x11\x01" + bytes((0x90,))
            + _s.pack(">H", 25) + _s.pack(">hhhh", 0, 0, 160, 200)
            + _s.pack(">hhhh", 0, 0, 160, 200) * 2 + _s.pack(">H", 0)
            + b"\x00" * (25 * 160) + b"\xff")
    fork = build_resource_fork([(b"scen", 128, scen), (b"PICT", 2011, b"preview"),
                                (b"PICT", 128, b"splash"), (b"PICT", 129, pict)])
    with tempfile.TemporaryDirectory() as src:
        path = os.path.join(src, "T")
        with open(path, "wb") as f:
            f.write(fork)
        out = os.path.join(src, "out")
        r = subprocess.run([sys.executable, sys.argv[1], "--file", path,
                            "--output", out, "--name", "T"],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
        # epic 2011 and splash 128 are skipped; other PICTs are kept.
        walls = os.listdir(os.path.join(out, "wallpapers"))
        assert not any("2011" in w or w == "picture-128.png" for w in walls), walls
        assert any("129" in w for w in walls), walls
        with open(os.path.join(out, "theme.conf")) as f:
            conf = f.read()
        assert "desktop-picture=Beach" in conf, conf
        assert "sound-set=Waves" in conf, conf
    print("ok    scen patt, epic exclusion and dpnm captured")


def test_chrome_slots():
    assert tool.chrome_slot("document window top") == "document-top"
    assert tool.chrome_slot("Document Window Top Inactive") == "document-top-inactive"
    assert tool.chrome_slot("WINDOW TITLE") == "document-top"
    assert tool.chrome_slot("Modal Bottom") == "modal-bottom"
    assert tool.chrome_slot("document window tab center") == "popup-tab"
    assert tool.chrome_slot("horz thumb grippy") is None
    assert tool.chrome_slot("Menu Bar Pressed") is None
    assert tool.chrome_slot("Menu background") == "menu-background"
    assert tool.chrome_slot("Menu Highlight") == "menu-highlight"
    assert tool.chrome_slot("Horz Thumb Stretch") == "scroll-thumb-horz-stretch"
    assert tool.chrome_slot("V Scroll Track") == "scroll-track-vert"
    assert tool.chrome_slot("Progress Bar Innards Active") == "progress-fill"
    assert tool.chrome_slot("Button Pressed") == "button-pressed"
    print("ok    chrome slot mapping")


def test_clut_positional():
    import importlib.util as _iu
    spec = _iu.spec_from_file_location("macicons", os.path.join(os.path.dirname(sys.argv[1]), "macicons.py"))
    mi = _iu.module_from_spec(spec); spec.loader.exec_module(mi)
    # System-file colour tables leave the ctEntry index 0 and use order.
    lookup = mi.clut_lookup([(0, (255, 0, 0)), (0, (0, 255, 0)), (0, (0, 0, 255))])
    assert lookup == [(255, 0, 0), (0, 255, 0), (0, 0, 255)], lookup
    print("ok    positional clut lookup")


def test_wctb():
    w = struct.pack(">6sH", b"\x00" * 6, 1) + struct.pack(">HHHH", 1, 0xFFFF, 0xCCCC, 0x9999) \
        + struct.pack(">HHHH", 7, 0xEEEE, 0xEEEE, 0xEEEE)
    out = tool.decode_wctb(w)
    assert out["window-frame"] == "ffcc99", out
    assert out["window-titlebar-light"] == "eeeeee", out
    print("ok    window colour table decoded")


def test_cicn():
    import importlib.util as _iu
    spec = _iu.spec_from_file_location("macicons", os.path.join(os.path.dirname(sys.argv[1]), "macicons.py"))
    mi = _iu.module_from_spec(spec); spec.loader.exec_module(mi)
    # 16x16 4-bit icon: PixMap + mask BitMap + bitmap BitMap + 4-byte handle
    # + mask data + bitmap data + ctab + pixel data.
    pixmap = struct.pack(">IHhhhhHHIIIHHHHIII", 0, 0x8008, 0, 0, 16, 16,
                         0, 0, 0, 72 << 16, 72 << 16, 0, 4, 1, 4, 0, 0, 0)
    bit_hdr = struct.pack(">IHhhhh", 0, 2, 0, 0, 16, 16)  # 1-bit BitMap, 2 bytes/row
    handle = struct.pack(">I", 0)
    mask = b"\xff" * 32            # 2 bytes/row * 16, all opaque
    bitmap = b"\x00" * 32          # 1-bit B&W copy, skipped by decoder
    ctab = struct.pack(">IHH", 0, 0, 1) + struct.pack(">HHHH", 0, 0, 0, 0) \
        + struct.pack(">HHHH", 1, 0xFFFF, 0xFFFF, 0xFFFF)
    pixel = b"\x11" * 128          # index 1 (white)
    rows = mi.decode_cicn(pixmap + bit_hdr + bit_hdr + handle + mask + bitmap + ctab + pixel)
    assert len(rows) == 16 and len(rows[0]) == 16, "16x16 rows"
    assert rows[0][0] == (255, 255, 255, 255), rows[0][0]
    print("ok    cicn decoded")


def test_pointers():
    import importlib.util as _iu
    import tempfile as _tf
    spec = _iu.spec_from_file_location("macicons", os.path.join(os.path.dirname(sys.argv[1]), "macicons.py"))
    mi = _iu.module_from_spec(spec); spec.loader.exec_module(mi)
    # A 'CURS': 16 rows of data, 16 rows of mask, hot spot (v, h) = (3, 5).
    # Pixel 0 is black (data and mask), pixel 1 is white (mask only).
    curs = bytearray(68)
    curs[0] = 0x80
    curs[32] = 0xC0
    struct.pack_into(">hh", curs, 64, 3, 5)
    rows, hot = mi.decode_curs(bytes(curs))
    assert hot == (5, 3), hot
    assert rows[0][0] == (0, 0, 0, 255), rows[0][0]
    assert rows[0][1] == (255, 255, 255, 255), rows[0][1]
    assert rows[0][2][3] == 0, "unmasked pixel is transparent"
    # A 'crsr' (16x16, 4-bit, embedded two-entry colour table): all pixels index 1, white.
    crsr = bytearray(298)
    struct.pack_into(">HII", crsr, 0, 0x8001, 96, 170)
    struct.pack_into(">IHhhhhHHIIIHHHHIII", crsr, 96, 0, 0x8008, 0, 0, 16, 16,
                     0, 0, 0, 72 << 16, 72 << 16, 0, 4, 1, 4, 0, 0, 0)
    struct.pack_into(">IHH", crsr, 146, 0, 0, 1)
    struct.pack_into(">HHHH", crsr, 154, 0, 0, 0, 0)
    struct.pack_into(">HHHH", crsr, 162, 1, 0xFFFF, 0xFFFF, 0xFFFF)
    crsr[170:298] = b"\x11" * 128
    struct.pack_into(">hh", crsr, 84, 3, 5)
    theme_raw = {"crsr": {128: bytes(crsr)}}
    system_raw = {"CURS": {i: bytes(curs) for i in range(-20721, -20714)}}
    with _tf.TemporaryDirectory() as out:
        assert tool.write_pointers(theme_raw, {}, system_raw, {}, out)
        cursors = os.path.join(out, "cursors")
        with open(os.path.join(cursors, "watch"), "rb") as f:
            blob = f.read()
        assert blob[:4] == b"Xcur"
        assert struct.unpack("<I", blob[12:16])[0] == 7 * 3, "7 frames at 16, 32 and 48 px"
        assert os.path.isfile(os.path.join(cursors, "wait"))
        assert os.path.isfile(os.path.join(cursors, "default")), "arrow from the theme's crsr 128"
        assert not os.path.exists(os.path.join(cursors, "left_side")), "no CURS -20852 supplied"
        with open(os.path.join(out, "index.theme")) as f:
            assert "Inherits=ZacOS9" in f.read()
    with _tf.TemporaryDirectory() as out:
        assert not tool.write_pointers({}, {}, {}, {}, out), "no pointers"
    print("ok    pointers written")


def test_system_sounds():
    import wave
    # A format 1 snd: one buffer command, a standard header, then 4 samples.
    head = struct.pack(">HHHI", 1, 1, 5, 0) + struct.pack(">H", 1)
    head += struct.pack(">HHI", 0x8051, 0, len(head) + 8)
    header = struct.pack(">IIIII", 0, 4, 22050 << 16, 0, 0) + struct.pack(">BB", 0, 60)
    snd = head + header + bytes((128, 200, 56, 128))
    with tempfile.TemporaryDirectory() as out:
        os.makedirs(os.path.join(out, "sounds"))
        tool.write_system_sounds({"snd ": {2: snd, 1: snd}}, out)
        names = sorted(os.listdir(os.path.join(out, "sounds")))
        assert names == ["system-sound-1.wav", "system-sound-2.wav"], names
        with wave.open(os.path.join(out, "sounds", "system-sound-1.wav")) as reader:
            assert reader.getnframes() == 4 and reader.getsampwidth() == 1
    print("ok    system sounds written")


def test_apple_menu():
    # A 16x16 8-bit icon whose every pixel is index 1, and a 256-entry palette.
    palette = [(i, (i, 0, 0)) for i in range(256)]
    with tempfile.TemporaryDirectory() as out:
        tool.write_apple_menu({b"ics8": [bytes([1]) * 256]}, [(9, palette)], out)
        path = os.path.join(out, "apple-menu.png")
        assert os.path.isfile(path), "apple-menu.png written"
        with open(path, "rb") as f:
            head = f.read(24)
        assert struct.unpack(">II", head[16:24]) == (16, 16)
    print("ok    apple menu icon written")


def test_standard_icon_ids():
    # Apple's Icons.h: kGenericHardDiskIconResource -3995 and
    # kGenericFileServerIconResource -3972. -3970 is the Suitcase.
    # Finder family 400 is the desktop printer; the disk is -3995. Its
    # 4-bit small member is decoded on the 16-grey table.
    kinds = {kind: rid for kind, _rtype, rid in tool.THEME_ICON_MAP}
    assert kinds["disk"] == -3995, "the hard disk is Apple's generic icon"
    assert kinds["network-disk"] == -3972, "the file server is Apple's standard icon"
    print("ok    hard disk and file server use Apple's standard icon ids")


def test_apple_menu_reads_icon_suite():
    # A Mac OS 9 theme ships its Apple menu icon as an icon suite (icns);
    # the collector must keep it so the theme's own Apple wins.
    path = tempfile.mkdtemp() + "/Focus"
    with open(path, "wb") as f:
        f.write(build_resource_fork([(b"icns", -16386, b"\x00" * 32)]))
    apple, icons, cursors, cluts = {}, {}, {}, []
    tool.collect_helper_fork(path, cluts, apple, icons, cursors)
    assert apple.get(b"icns"), "a theme's Apple icon suite is collected"
    print("ok    the Apple menu reads a theme's icon suite")


def test_wallpaper_size_filter():
    def pict_resource(w, h):
        # A version-1 BitsRect PICT the decoder accepts (main's fixture
        # shape): frame, version opcode, rowbytes, bitmap/src/dst rects,
        # zero pixel rows, end opcode.
        import struct as _s
        rowbytes = (w + 7) // 8
        return (_s.pack(">H", 0) + _s.pack(">hhhh", 0, 0, h, w) + b"\x11\x01" + bytes((0x90,))
                + _s.pack(">H", rowbytes) + _s.pack(">hhhh", 0, 0, h, w)
                + _s.pack(">hhhh", 0, 0, h, w) * 2 + _s.pack(">H", 0)
                + b"\x00" * (rowbytes * h) + b"\xff")

    out = tempfile.mkdtemp() + "/theme"
    for sub in ("wallpapers", "patterns", "sounds"):
        os.makedirs(os.path.join(out, sub))
    tool.write_assets({"pict": [(1, pict_resource(32, 32)), (2, pict_resource(640, 480))]}, set(), out)
    pics = os.listdir(os.path.join(out, "wallpapers"))
    assert len(pics) == 1, f"icon-sized PICT must not become a wallpaper: {pics}"
    print("ok    icon-sized PICTs are not wallpapers")


def test_scen():
    data = build_scen([
        (b"name", b"\x0bConvergence"),
        (b"coll", b"\x00"),
        (b"smoo", b"\x01"),
        (b"vfsz", struct.pack(">h", 10)),
        (b"smos", struct.pack(">H", 12)),
        (b"hcol", struct.pack(">HHH", 0x9999, 0xCCCC, 0xFFFF)),
        (b"sbar", struct.pack(">H", 1)),
        (b"sbth", struct.pack(">H", 0)),
    ])
    tags = tool.decode_scen_collection(data)
    assert tags["name"] == "Convergence", tags
    assert tags["coll"] is False, tags
    assert tags["smoo"] is True, tags
    assert tags["vfsz"] == 10, tags
    assert tags["smos"] == 12, tags
    assert tags["hcol"] == (0x9999, 0xCCCC, 0xFFFF), tags
    assert tags["sbar"] == 1, tags
    assert tags["sbth"] == 0, tags
    print("ok    scen collection decoded")


def test_theme_icons_share_system_palette():
    # The gray-ramp rule is gone: every icon decodes against the System
    # tables, so a photo-sized table is fine anywhere in the order.
    photo = [(i, (i, 255 - i, 128)) for i in range(256)]
    gray = [(i, (10, 10, 10)) for i in range(8)]
    assert tool.macicons.resolve_palette([(9, photo)])[42][1] == (204, 204, 255), \
        "the System palette calibrates folder fill whatever the table"
    print("ok    icons share the System palette")


def test_theme_art_writes_no_boot():
    gray = [(i, (i * 8, i * 8, i * 8)) for i in range(8)]
    out = tempfile.mkdtemp() + "/theme"
    os.makedirs(out)
    tool.write_theme_art({b"icl8": [b"\x00" * 1024], b"ICN#": [b"\x00" * 256]},
                         None, [(5, gray)], out)
    assert os.path.isfile(os.path.join(out, "menu-icon.png")), "menu icon written"
    boot = os.path.join(out, "boot")
    assert not os.path.isdir(boot) or not os.listdir(boot), \
        "the theme file icon must not stand in for boot art"
    print("ok    theme icon is not cloned into boot art")


def _gray_clut_body(n=8):
    out = struct.pack(">IHH", 0, 0, n - 1)
    for i in range(n):
        out += struct.pack(">HHHH", i, i * 257, i * 257, i * 257)
    return out


def _png_size(path):
    with open(path, "rb") as f:
        head = f.read(26)
    assert head[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    w, h = struct.unpack(">II", head[16:24])
    return w, h


def _sfnt_head():
    return struct.pack(">II", 0x00010000, 0) + struct.pack(">I", 0) + \
        struct.pack(">IHH", 0x5F0F3CF5, 0, 2048) + b"\x00" * 16 + \
        struct.pack(">hhhhhhhhh", 0, 0, 0, 0, 0, 8, 2, 0, 0)


def _sfnt_resource(family, subfamily):
    recs, strings, off = b"", b"", 0
    for name_id, text in ((1, family), (2, subfamily)):
        raw = text.encode("utf-16-be")
        recs += struct.pack(">HHHHHH", 3, 1, 0x409, name_id, len(raw), off)
        strings += raw
        off += len(raw)
    name_table = struct.pack(">HHH", 0, 2, 6 + 24) + recs + strings
    head = _sfnt_head()
    tables = [(b"name", name_table), (b"head", head)]
    out = struct.pack(">IHHHH", 0x00010000, len(tables), 0, 0, 0)
    pos = 12 + 16 * len(tables)
    for tag, body in tables:
        out += tag + struct.pack(">III", 0, pos, len(body))
        pos += len(body)
    for _tag, body in tables:
        out += body
    return out


def test_sfnt_to_ttf():
    out = tempfile.mkdtemp() + "/theme"
    for sub in ("wallpapers", "patterns", "sounds", "fonts"):
        os.makedirs(os.path.join(out, sub))
    tool.write_assets({"font": [_sfnt_resource("Charcoal", "Regular"), b"junk"]}, set(), out)
    faces = sorted(os.listdir(os.path.join(out, "fonts")))
    assert faces == ["Charcoal.ttf"], f"sfnt decodes to a named face: {faces}"
    with open(os.path.join(out, "fonts", faces[0]), "rb") as f:
        installed = f.read()
    assert installed[:4] == b"\x00\x01\x00\x00", "the face installs as a font"
    assert tool.macfont.parse_sfnt_names(installed) == ("Charcoal", "Regular")
    print("ok    sfnt resources decode to installable faces")

def _curs_resource(hot=(7, 4)):
    image = b"\xff" * 32
    mask = b"\xff" * 16 + b"\x00" * 16
    return image + mask + struct.pack(">HH", hot[1], hot[0])


def test_cursor_resources_decode():
    out = tempfile.mkdtemp() + "/theme"
    for sub in ("wallpapers", "patterns", "sounds", "fonts", "cursors", "icons"):
        os.makedirs(os.path.join(out, sub))
    tool.write_theme_cursors({(b"CURS", 1): [_curs_resource((7, 4))]}, out)
    assert _png_size(os.path.join(out, "cursors", "xterm-16.png")) == (16, 16)
    import json as _json
    with open(os.path.join(out, "cursors", "cursors.json")) as f:
        meta = _json.load(f)
    assert meta["xterm"]["file"] == "xterm-16.png", meta
    assert meta["xterm"]["hot"] == [7, 4], "hotspot survives the import"
    rows, hot = tool.decode_cursor(_curs_resource((7, 4)))
    assert hot == (7, 4)
    assert rows[0][0][3] == 255 and rows[15][0][3] == 0, "mask clears outside the shape"
    print("ok    cursor resources decode with hotspots and masks")


def test_file_mode_finds_apple_menu():
    # A theme file inside a Mac OS 9 tree: --file mode still picks up the
    # System file's Apple menu icon for apple-menu.png and the menu icon.
    # HOME is pointed at the temp tree so ~/Classic cannot interfere.
    root = tempfile.mkdtemp()
    os.environ["HOME"] = root
    sys_dir = os.path.join(root, "System Folder", ".rsrc")
    os.makedirs(sys_dir)
    theme_dir = os.path.join(root, "System Folder", "Appearance", "Theme Files", ".rsrc")
    os.makedirs(theme_dir)
    apple = build_resource_fork([
        (b"ics8", -16386, b"\x07" * 256),
        (b"ics#", -16386, b"\x00" * 64),
        (b"clut", 9, _gray_clut_body()),
        (b"icl8", -3999, b"\x2a" * 1024),
        (b"ICN#", -3999, b"\x00" * 128 + b"\xff" * 64 + b"\x00" * 64),
        (b"CURS", 1, b"\x00" * 32 + b"\xff" * 16 + b"\x00" * 16 + b"\x00\x07\x00\x04"),
    ])
    with open(os.path.join(sys_dir, "System"), "wb") as f:
        f.write(apple)
    finder = build_resource_fork([
        (b"icl8", 7202, b"\x17" * 1024),
        (b"ICN#", 7202, b"\xff" * 256),
        (b"CURS", 6604, b"\x00" * 32 + b"\xff" * 16 + b"\x00" * 16 + b"\x00\x02\x00\x02"),
    ])
    with open(os.path.join(sys_dir, "Finder"), "wb") as f:
        f.write(finder)
    theme = build_resource_fork([
        (b"icl8", 1024, b"\x00" * 1024),
        (b"ICN#", 1024, b"\x00" * 256),
        (b"clut", 200, _gray_clut_body()),
    ])
    theme_path = os.path.join(theme_dir, "Theme")
    with open(theme_path, "wb") as f:
        f.write(theme)
    assert tool.find_mac_file(theme_path, "System") == os.path.join(sys_dir, "System"), \
        "the System file is found by climbing from the theme file"
    out = tempfile.mkdtemp() + "/theme"
    r = subprocess.run(
        [sys.executable, sys.argv[1], "--file", theme_path, "--output", out, "--name", "T"],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    assert os.path.isfile(os.path.join(out, "apple-menu.png")), "Apple menu icon written"
    assert _png_size(os.path.join(out, "apple-menu.png")) == (16, 16)
    assert _png_size(os.path.join(out, "menu-icon.png")) == (20, 15), \
        "the Zac menu slot shows the Apple icon, not the theme file icon"
    assert _png_size(os.path.join(out, "icons", "folder-32.png")) == (32, 32)
    assert _png_size(os.path.join(out, "icons", "folder-16.png")) == (16, 16)
    assert _png_size(os.path.join(out, "icons", "caution-32.png")) == (32, 32)
    assert _png_size(os.path.join(out, "icons", "caution-16.png")) == (16, 16), \
        "mapped desktop icons decode at both sizes, caution from the Finder file"
    assert _png_size(os.path.join(out, "cursors", "xterm-16.png")) == (16, 16)
    assert _png_size(os.path.join(out, "cursors", "hand2-16.png")) == (16, 16), \
        "mapped pointers decode, hands from the Finder file"
    with open(os.path.join(out, "cursors", "Macintosh", "index.theme")) as _tf:
        index_theme = _tf.read()
    assert "Inherits=ZacOS9" in index_theme, "unmapped cursors fall back to ZacOS9"
    with open(os.path.join(out, "cursors", "Macintosh", "cursors", "xterm"), "rb") as _xf:
        assert _xf.read(4) == b"Xcur", "pointers ship as real Xcursor files"
    # The calibration applies: index 42 decodes to the desktop folder's
    # periwinkle even against a gray clut.
    gray = tool.parse_clut(_gray_clut_body())
    table = dict(tool.macicons.resolve_palette([(9, gray)]))
    assert table[42] == (204, 204, 255), "folder fill calibrates to periwinkle"
    # The mask cuts transparency: opaque top rows, clear bottom rows.
    rows = tool.decode_theme_icon({(b"icl8", -3999): [b"\x2a" * 1024],
        (b"ICN#", -3999): [b"\x00" * 128 + b"\xff" * 64 + b"\x00" * 64]},
        tool.macicons.resolve_palette([(9, gray)]), b"icl8", -3999, 32, 1024)
    assert rows[0][0][3] == 255 and rows[31][0][3] == 0, "masks stay transparent"
    print("ok    --file mode finds the System file's Apple menu icon")


def test_write_theme_pfonts():
    import importlib.util as _iu
    root = os.path.join(os.path.dirname(sys.argv[1]), "fonts", "build_fonts.py")
    spec = _iu.spec_from_file_location("build_fonts", root)
    bf = _iu.module_from_spec(spec)
    spec.loader.exec_module(bf)
    candidates = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu-core/DejaVuSans.ttf",
    ]
    ttf = next((p for p in candidates if os.path.isfile(p)), None)
    assert ttf is not None, "DejaVu Sans must be installed (fonts-dejavu-core)"
    out = tempfile.mkdtemp() + "/theme"
    os.makedirs(os.path.join(out, "fonts"))
    # The theme's own faces (named by its scen collection) rasterize at
    # text sizes, 1x and the fractional scales, into valid .pfont files.
    n = tool.write_theme_pfonts(out, {"lgsf": "DejaVu Sans", "vfnt": "DejaVu Sans"},
        {"DejaVu Sans": ttf})
    assert n == 8, f"system 12/15/18/21 plus views 11/14/17/19: {n}"
    for role, size in (("system", 12), ("system", 15), ("system", 18), ("system", 21),
            ("views", 11), ("views", 14), ("views", 17), ("views", 19)):
        path = os.path.join(out, "fonts", "theme-%s-%d.pfont" % (role, size))
        assert os.path.isfile(path), f"missing {path}"
        font = bf.Font(path)
        assert len(font.glyphs) > 100, f"{path} keeps its glyphs"
    # A theme naming a face it did not bring writes nothing (built-ins hold).
    out2 = tempfile.mkdtemp() + "/theme"
    os.makedirs(os.path.join(out2, "fonts"))
    assert tool.write_theme_pfonts(out2, {"lgsf": "Charcoal"}, {}) == 0
    assert os.listdir(os.path.join(out2, "fonts")) == []
    # A face that will not rasterize is skipped, not fatal.
    out3 = tempfile.mkdtemp() + "/theme"
    os.makedirs(os.path.join(out3, "fonts"))
    assert tool.write_theme_pfonts(out3, {"lgsf": "Broken"}, {"Broken": "/no/such.ttf"}) == 0
    assert os.listdir(os.path.join(out3, "fonts")) == []
    print("ok    theme window fonts rasterize from the theme's own faces")


def test_filtered_png_reads():
    import struct as _s
    import zlib as _z
    # A 2x2 RGBA image written with PNG filters (Sub on row 0, Up on row
    # 1): hand-placed art from other tools must read like our own output.
    row0 = [10, 0, 0, 255, 15, 10, 0, 255]
    filt0 = [1] + [row0[0], row0[1], row0[2], row0[3]] + \
        [(row0[4 + i] - row0[i]) & 0xFF for i in range(4)]
    row1 = [10, 20, 0, 255, 10, 20, 0, 255]
    filt1 = [2] + [(row1[i] - row0[i]) & 0xFF for i in range(8)]
    blob = _z.compress(bytes(filt0) + bytes(filt1))
    ihdr = _s.pack(">IIBBBBB", 2, 2, 8, 6, 0, 0, 0)

    def chunk(typ, data):
        out = _s.pack(">I", len(data)) + typ + data
        return out + _s.pack(">I", 0xFFFFFFFF & _z.crc32(typ + data))

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", blob) + \
        chunk(b"IEND", b"")
    path = os.path.join(tempfile.mkdtemp(), "filtered.png")
    with open(path, "wb") as f:
        f.write(png)
    rows = tool._read_png_rgba(path)
    assert rows == [[(10, 0, 0, 255), (15, 10, 0, 255)],
        [(10, 20, 0, 255), (10, 20, 0, 255)]], rows
    print("ok    filtered PNGs (hand-placed art) read correctly")


def _red_clut():
    # A gray-headed icon palette whose index 1 is red: the theme's own.
    out = struct.pack(">IHH", 0, 0, 1)
    out += struct.pack(">HHHH", 0, 5, 5, 5)
    out += struct.pack(">HHHH", 1, 0xC800, 0x1000, 0x1000)
    return out


def test_theme_own_icons_win():
    # A dropped theme file carrying its own desktop art: the bundle uses
    # its art, not the System file's — but renders it through the System
    # palette, the table icons display through (a scheme's own tables
    # describe its chrome; Aqua's folder decoded brown through its own).
    root = tempfile.mkdtemp()
    os.environ["HOME"] = root
    sys_dir = os.path.join(root, "Classic", "System Folder", ".rsrc")
    os.makedirs(sys_dir)
    gray256 = struct.pack(">IHH", 0, 0, 255)
    for i in range(256):
        gray256 += struct.pack(">HHHH", i, i * 257, i * 257, i * 257)
    system = build_resource_fork([
        (b"icl8", -3999, b"\x09" * 1024),
        (b"ICN#", -3999, b"\xff" * 256),
        (b"clut", 9, gray256),
    ])
    with open(os.path.join(sys_dir, "System"), "wb") as f:
        f.write(system)
    red = b"\x01" * 1024
    theme = build_resource_fork([
        (b"icl8", -3999, red),
        (b"ICN#", -3999, b"\xff" * 256),
        (b"clut", 800, _red_clut()),
    ])
    theme_path = os.path.join(root, "Theme")
    with open(theme_path, "wb") as f:
        f.write(theme)
    out = tempfile.mkdtemp() + "/theme"
    r = subprocess.run(
        [sys.executable, sys.argv[1], "--file", theme_path, "--output", out, "--name", "T"],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    folder = os.path.join(out, "icons", "folder-32.png")
    assert os.path.isfile(folder), "the theme's own folder decodes"
    with open(folder, "rb") as f:
        head = f.read(26)
    w, h = struct.unpack(">II", head[16:24])
    assert (w, h) == (32, 32)
    # Palette check: the theme's ink (index 1) renders as the System
    # table's gray 1, not the theme file's red and not the System
    # file's decoy ink (index 9).
    import zlib as _z
    with open(folder, "rb") as f:
        blob = f.read()
    pos, raw = 8, b""
    while pos + 8 <= len(blob):
        (length,) = struct.unpack(">I", blob[pos:pos + 4])
        typ = blob[pos + 4:pos + 8]
        if typ == b"IDAT":
            raw += blob[pos + 8:pos + 8 + length]
        pos += 12 + length
    px = _z.decompress(raw)
    # First row: filter byte + 32 RGBA pixels; all ink is index 1.
    assert px[1:5] == bytes((1, 1, 1, 255)), px[1:9].hex()
    print("ok    a theme file's own icons win, via the System palette")


def test_write_unique_reuses_identical():
    out = tempfile.mkdtemp()
    first = tool.write_unique(out, "a.bin", b"same")
    again = tool.write_unique(out, "a.bin", b"same")
    assert first == again, "re-importing identical bytes reuses the file"
    assert os.listdir(out) == ["a.bin"], os.listdir(out)
    other = tool.write_unique(out, "a.bin", b"different")
    assert other != first and os.path.isfile(other), "different bytes still uniquify"
    print("ok    re-imports reuse identical files instead of duplicating")


def _tinted_clut_body():
    # An 8-entry table that starts gray but runs blue: the old
    # auto-colour rule picked it and dyed the chrome blue.
    out = struct.pack(">IHH", 0, 0, 7)
    for i, (r, g, b) in enumerate([(238, 238, 238), (204, 204, 255), (153, 153, 255),
            (102, 102, 204), (51, 51, 153), (0, 0, 136), (0, 0, 85), (0, 0, 0)]):
        out += struct.pack(">HHHH", i, r * 257, g * 257, b * 257)
    return out


def _platinum_clut_body():
    # A true 8-entry gray ramp in Platinum tones.
    out = struct.pack(">IHH", 0, 0, 7)
    for i, v in enumerate([0xEE, 0xCC, 0xAA, 0x77, 0x55, 0x33, 0x11, 0x00]):
        out += struct.pack(">HHHH", i, v * 257, v * 257, v * 257)
    return out


def _theme_with_cluts():
    root = tempfile.mkdtemp()
    # Tinted first (old code picked it), true gray second.
    fork = build_resource_fork([
        (b"icl8", -3999, b"\x00" * 1024),
        (b"ICN#", -3999, b"\xff" * 256),
        (b"clut", 208, _tinted_clut_body()),
        (b"clut", 216, _platinum_clut_body()),
    ])
    path = os.path.join(root, "Theme")
    with open(path, "wb") as f:
        f.write(fork)
    return path


def test_auto_colours_need_true_gray():
    # A tinted ramp listed first must not dye the window chrome: only a
    # real gray ramp sets it, else the built-in platinum grays show.
    out = tempfile.mkdtemp() + "/theme"
    os.makedirs(os.path.join(out, "icons"))
    r = subprocess.run(
        [sys.executable, sys.argv[1], "--file", _theme_with_cluts(),
         "--output", out, "--name", "T", "--auto-colours"],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    conf = {}
    with open(os.path.join(out, "theme.conf")) as f:
        for line in f:
            if "=" in line:
                k, v = line.strip().split("=", 1)
                conf[k] = v
    assert conf.get("title-stripe-dark") == "777777", conf
    assert conf.get("frame-dark") == "aaaaaa", conf
    assert conf.get("frame-face") == "cccccc", conf
    # Without any true gray ramp the chrome keys stay out entirely.
    out2 = tempfile.mkdtemp() + "/theme"
    os.makedirs(os.path.join(out2, "icons"))
    blue_only = build_resource_fork([
        (b"icl8", -3999, b"\x00" * 1024),
        (b"ICN#", -3999, b"\xff" * 256),
        (b"clut", 208, _tinted_clut_body()),
    ])
    theme2 = os.path.join(tempfile.mkdtemp(), "Theme")
    with open(theme2, "wb") as f:
        f.write(blue_only)
    r = subprocess.run(
        [sys.executable, sys.argv[1], "--file", theme2,
         "--output", out2, "--name", "T2", "--auto-colours"],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    with open(os.path.join(out2, "theme.conf")) as f:
        body = f.read()
    assert "title-stripe-dark" not in body, body
    assert "frame-face" not in body, body
    print("ok    auto colours need a true gray ramp")


def _ppat_body():
    # Minimal 8x8 8-bit ppat: checkerboard of black and white.
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


def test_pattern_chrome_approximates():
    # Without a true gray ramp the chrome comes from the theme's own
    # desktop tile (here a black/white checkerboard, average 0x7F):
    # explicit flags still win over it.
    root = tempfile.mkdtemp()
    fork = build_resource_fork([
        (b"icl8", -3999, b"\x00" * 1024),
        (b"ICN#", -3999, b"\xff" * 256),
        (b"clut", 208, _tinted_clut_body()),
        (b"ppat", 1000, _ppat_body()),
    ])
    path = os.path.join(root, "Theme")
    with open(path, "wb") as f:
        f.write(fork)
    out = tempfile.mkdtemp() + "/theme"
    os.makedirs(os.path.join(out, "icons"))
    r = subprocess.run(
        [sys.executable, sys.argv[1], "--file", path,
         "--output", out, "--name", "T", "--auto-colours"],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    conf = {}
    with open(os.path.join(out, "theme.conf")) as f:
        for line in f:
            if "=" in line:
                k, v = line.strip().split("=", 1)
                conf[k] = v
    # Checkerboard average 0x7F lightened halfway: 0xBF.
    assert conf.get("title-base") == "bfbfbf", conf
    assert conf.get("frame-face") == "9f9f9f", conf
    out2 = tempfile.mkdtemp() + "/theme"
    os.makedirs(os.path.join(out2, "icons"))
    r = subprocess.run(
        [sys.executable, sys.argv[1], "--file", path,
         "--output", out2, "--name", "T2", "--auto-colours",
         "--title-base", "FF0000", "--frame-face", "00FF00"],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    conf2 = {}
    with open(os.path.join(out2, "theme.conf")) as f:
        for line in f:
            if "=" in line:
                k, v = line.strip().split("=", 1)
                conf2[k] = v
    assert conf2.get("title-base") == "FF0000", conf2
    assert conf2.get("frame-face") == "00FF00", conf2
    print("ok    desktop tile approximates missing chrome, flags win")


def test_theme_system_folder():
    # The System Folder's own icon family (-3983) rides along like the
    # other mapped desktop icons.
    root = tempfile.mkdtemp()
    os.environ["HOME"] = root
    red = b"\x01" * 1024
    theme = build_resource_fork([
        (b"icl8", -3983, red),
        (b"ICN#", -3983, b"\xff" * 256),
        (b"clut", 800, _red_clut()),
    ])
    theme_path = os.path.join(root, "Theme")
    with open(theme_path, "wb") as f:
        f.write(theme)
    out = tempfile.mkdtemp() + "/theme"
    r = subprocess.run(
        [sys.executable, sys.argv[1], "--file", theme_path, "--output", out, "--name", "T"],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    folder = os.path.join(out, "icons", "system-folder-32.png")
    assert os.path.isfile(folder), "the theme's own System Folder decodes"
    import zlib as _z
    with open(folder, "rb") as f:
        blob = f.read()
    pos, raw = 8, b""
    while pos + 8 <= len(blob):
        (length,) = struct.unpack(">I", blob[pos:pos + 4])
        if blob[pos + 4:pos + 8] == b"IDAT":
            raw += blob[pos + 8:pos + 8 + length]
        pos += 12 + length
    px = _z.decompress(raw)
    assert px[1:5] == bytes((200, 16, 16, 255)), px[1:9].hex()
    print("ok    the System Folder's own icon maps like the rest")


def _png_pixels(path):
    import zlib as _z
    with open(path, "rb") as f:
        blob = f.read()
    pos, raw = 8, b""
    while pos + 8 <= len(blob):
        (length,) = struct.unpack(">I", blob[pos:pos + 4])
        if blob[pos + 4:pos + 8] == b"IDAT":
            raw += blob[pos + 8:pos + 8 + length]
        pos += 12 + length
    return _z.decompress(raw)


def test_file_icon_decodes():
    # The theme file's own Finder icon (family 1024) lands in the bundle
    # so the Sniffer can wear it; files without one stay silent.
    root = tempfile.mkdtemp()
    os.environ["HOME"] = root
    sys_dir = os.path.join(root, "Classic", "System Folder", ".rsrc")
    os.makedirs(sys_dir)
    gray256 = struct.pack(">IHH", 0, 0, 255)
    for i in range(256):
        gray256 += struct.pack(">HHHH", i, i * 257, i * 257, i * 257)
    with open(os.path.join(sys_dir, "System"), "wb") as f:
        f.write(build_resource_fork([(b"clut", 9, gray256)]))
    theme = build_resource_fork([
        (b"icl8", 1024, b"\x05" * 1024),
        (b"ICN#", 1024, b"\xff" * 256),
        (b"clut", 800, _red_clut()),
    ])
    theme_path = os.path.join(root, "Theme")
    with open(theme_path, "wb") as f:
        f.write(theme)
    out = tempfile.mkdtemp() + "/theme"
    r = subprocess.run(
        [sys.executable, sys.argv[1], "--file", theme_path, "--output", out, "--name", "T"],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    icon = os.path.join(out, "file-icon.png")
    assert os.path.isfile(icon), "the theme file's own icon decodes"
    assert _png_pixels(icon)[1:5] == bytes((255, 255, 0, 255)), \
        "index 5 calibrates to caution yellow through the System palette"
    # No 1024 family: no file icon, no failure.
    bare = build_resource_fork([(b"clut", 800, _red_clut())])
    bare_path = os.path.join(root, "Bare")
    with open(bare_path, "wb") as f:
        f.write(bare)
    out2 = tempfile.mkdtemp() + "/theme"
    r = subprocess.run(
        [sys.executable, sys.argv[1], "--file", bare_path, "--output", out2, "--name", "B"],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    assert not os.path.isfile(os.path.join(out2, "file-icon.png"))
    print("ok    the theme file's own icon rides along for the Sniffer")


def _pxm_body(w=13, h=13, layers=2):
    # Minimal pxm#: 24-byte header, all-set mask, layers of 16-bit white.
    head = struct.pack(">HHIHHH", 1, 1, 0, w, h, 16) + b"\x00" * 6 + b"\x07\xda" + struct.pack(">H", layers)
    assert len(head) == 24, len(head)
    mask = b"\xff" * (h * 2)
    px = struct.pack(">H", 0x7FFF) * (w * h)
    return head + mask + px * layers


def test_theme_chrome_decodes():
    # pxm# layers decode with their mask; the lined-up theme's bar tiles
    # resolve to its pattern files and its buttons land in chrome/.
    rows = tool.decode_pxm(_pxm_body(2, 2, 1), 0)
    assert rows == [[(255, 255, 255, 255)] * 2, [(255, 255, 255, 255)] * 2], rows
    try:
        tool.decode_pxm(_pxm_body(2, 2, 1), 1)
        assert False, "only one layer here"
    except ValueError:
        pass
    out = tempfile.mkdtemp() + "/theme"
    os.makedirs(os.path.join(out, "patterns"))
    pxm = {200: [_pxm_body()], 210: [_pxm_body()], 220: [_pxm_body()]}
    # Minimal 8x8 ppat bodies stand in for the bar tiles; only ids matter.
    ppats = [(1010, _ppat_body()), (3102, _ppat_body()),
        (1014, _ppat_body()), (20609, _ppat_body())]
    keys = tool.write_theme_chrome("Aqua v2", pxm, ppats, out)
    assert keys.get("bar-middle", "").endswith("chrome/bar-middle.argb"), keys
    assert keys.get("button-close", "") == "chrome/button-close.argb", keys
    assert keys.get("button-close-pressed", "").endswith("-pressed.argb"), keys
    assert os.path.isfile(os.path.join(out, "chrome", "button-grow.argb"))
    assert tool.write_theme_chrome("No Such Theme", pxm, ppats, out) == {}
    print("ok    lined-up window art decodes into chrome/")


def test_bar_tiles_align():
    # A cap resource can carry rows above the bar (or the frame below it):
    # every bar tile is saved at the middle's height from its first inked
    # row, so the ends are not shorter than the middle.
    pxm = {pid: [_pxm_body()] for pid in (200, 210, 220)}
    pxm[6176] = [_pxm_body(13, 22)]  # left cap: 13 rows, 22 columns
    pxm[6173] = [_pxm_body(13, 22)]  # right cap
    pxm[6170] = [_pxm_body(6, 22)]   # middle: 6 rows
    layo = {128: _layo_body([
        (b"pxm#", 6176, 256, 0),
        (b"pxm#", 6170, 256, 0),
        (b"pxm#", 6173, 256, 0),
        (b"pxm#", 200, 0, 10), (b"pxm#", 210, 10, 10), (b"pxm#", 220, 20, 10),
    ])}
    out = tempfile.mkdtemp() + "/theme"
    tool.write_theme_chrome("Bar", pxm, [], out, layo=layo)
    heights = {}
    for key in ("bar-left", "bar-middle", "bar-right"):
        with open(os.path.join(out, "chrome", key + ".png"), "rb") as f:
            head = f.read(24)
        heights[key] = struct.unpack(">I", head[20:24])[0]
    assert heights == {"bar-left": 6, "bar-middle": 6, "bar-right": 6}, heights
    print("ok    bar caps are aligned to the middle's height")


def _layo_body(entries):
    # entries: [(tag, rid, group, lead)]; one 40-byte record each.
    out = b"\x00" * 20
    for tag, rid, group, lead in entries:
        pre = [0] * 10
        pre[1] = group
        pre[4] = lead
        out += struct.pack(">10h", *pre) + tag + struct.pack(">h", rid) + struct.pack(">7h", 0, 0, 0, 0, 0, 0, 0)
    return out


def _plut_body(mapping):
    # mapping: idx -> (ppat_id, (r, g, b)); idx = ~rid.
    n = max(mapping) + 1 if mapping else 0
    out = b"\x00" * 6 + struct.pack(">H", n)
    for k in range(n):
        pid, rgb = mapping.get(k, (0, (255, 255, 255)))
        out += struct.pack(">HHHH", pid, rgb[0] << 8, rgb[1] << 8, rgb[2] << 8)
    return out


def _ppat_body_wide(w=64, h=16):
    # Checkerboard pixmap pattern of any size (macpat reads geometry
    # from the PixMap header).
    pixmap = struct.pack(">H4HHHI", 0x8000 | w, 0, 0, h, w, 0, 0, 0)
    pixmap += struct.pack(">IIHHHHIII", 0x00480000, 0x00480000, 0, 8, 1, 8, 0, 0, 0)
    assert len(pixmap) == 46
    pixels = bytes((x + y) % 2 for y in range(h) for x in range(w))
    clut_off = 78 + len(pixels)
    pixmap = pixmap[:38] + struct.pack(">I", clut_off) + pixmap[42:]
    clut = struct.pack(">IHH", 0, 0, 1) + struct.pack(">HHHH", 0, 0, 0, 0) + \
        struct.pack(">HHHH", 1, 0xFFFF, 0xFFFF, 0xFFFF)
    return struct.pack(">H", 1) + struct.pack(">I", 28) + struct.pack(">I", 78) + b"\x00" * 10 + \
        b"\x00" * 8 + b"\x00" * 4 + pixmap + pixels + clut


def _chrome_layo_128(lead):
    # First-block records: left cap, patch fill, right cap, then the
    # title buttons sharing one zone each.
    return _layo_body([
        (b"pxm#", 6176, 256, 0),
        (b"plut", -90, 256, 0),
        (b"pxm#", 6173, 256, 0),
        (b"pxm#", 200, 0, 38),
        (b"pxm#", 210, 24, lead),
        (b"pxm#", 220, 0, lead),
    ])


def test_theme_chrome_twopart():
    # A left-clustered theme (all button zones 38) gains a bar-span from
    # the frame layout's active/inactive groups plus title-cluster=left;
    # a mixed-zone theme keeps its single span.
    pxm = {pid: [_pxm_body()] for pid in (200, 210, 220)}
    pxm.update({pid: [_pxm_body(22, 13)] for pid in (6176, 6173)})
    pxm.update({6185: [_pxm_body()], 211: [_pxm_body()], 221: [_pxm_body()]})
    plut = {387: _plut_body({89: (9001, (255, 255, 255)),
        112: (9002, (255, 255, 255)), 28: (9003, (255, 255, 255))})}
    ppats = [(9001, _ppat_body()), (9002, _ppat_body_wide()),
        (9003, _ppat_body_wide(48, 8))]
    layo = {128: _chrome_layo_128(38), 129: _chrome_layo_128(38),
        134: _chrome_layo_128(38), 135: _chrome_layo_128(38),
        430: _layo_body([(b"plut", -113, 1, 0), (b"plut", -29, 0, 0)])}
    out = tempfile.mkdtemp() + "/theme"
    os.makedirs(os.path.join(out, "patterns"))
    keys = tool.write_theme_chrome("Aqua v2", pxm, ppats, out, {}, layo, plut)
    assert keys.get("title-cluster") == "left", keys
    assert keys.get("bar-span", "").endswith("chrome/bar-span.argb"), keys
    assert keys.get("bar-span-inactive", "").endswith("chrome/bar-span-inactive.argb"), keys
    assert os.path.isfile(os.path.join(out, "chrome", "bar-span.argb"))
    assert os.path.isfile(os.path.join(out, "chrome", "bar-span-inactive.argb"))
    assert keys.get("bar-dialog-middle", "").endswith("chrome/bar-dialog-middle.argb"), keys
    assert keys.get("bar-dialog-middle-inactive", "").endswith("chrome/bar-dialog-middle-inactive.argb"), keys
    assert keys.get("button-dialog-close", "").endswith("chrome/button-dialog-close.argb"), keys
    assert keys.get("button-dialog-zoom", "").endswith("chrome/button-dialog-zoom.argb"), keys
    assert os.path.isfile(os.path.join(out, "chrome", "bar-dialog-left.argb"))
    # Same art, but a right-pinned zoom box: no span, no cluster.
    layo2 = dict(layo)
    layo2[128] = _chrome_layo_128(5)
    out2 = tempfile.mkdtemp() + "/theme"
    os.makedirs(os.path.join(out2, "patterns"))
    keys2 = tool.write_theme_chrome("Plain", pxm, ppats, out2, {}, layo2, plut)
    assert "title-cluster" not in keys2, keys2
    assert "bar-span" not in keys2, keys2
    assert "bar-span-inactive" not in keys2, keys2
    print("ok    left-clustered themes gain a bar span; others keep one span")


test_parse()
test_end_to_end()
test_clut_positional()
test_wctb()
test_cicn()
test_chrome_slots()
test_scen()
test_pointers()
test_system_sounds()
test_apple_menu()
test_standard_icon_ids()
test_apple_menu_reads_icon_suite()
test_scen_patt_epic()
test_wallpaper_size_filter()
test_theme_icons_share_system_palette()
test_theme_art_writes_no_boot()
test_sfnt_to_ttf()
test_file_mode_finds_apple_menu()
test_write_theme_pfonts()
test_filtered_png_reads()
test_theme_own_icons_win()
test_write_unique_reuses_identical()
test_auto_colours_need_true_gray()
test_pattern_chrome_approximates()
test_theme_system_folder()
test_file_icon_decodes()
test_theme_chrome_decodes()
test_theme_chrome_twopart()
test_bar_tiles_align()


def test_theme_icons_scale_and_clear_stale():
    palette = [(i, (i, i, i)) for i in range(256)]
    out = tempfile.mkdtemp() + "/theme"
    icons = os.path.join(out, "icons")
    os.makedirs(icons)
    stale = os.path.join(icons, "disk-32.png")
    with open(stale, "wb") as f:
        f.write(b"old")
    tool.write_theme_icons({}, [(9, palette)], out)
    assert not os.path.exists(stale), "a stale icon is removed when the source lacks it"
    assert not os.path.exists(os.path.join(icons, "disk-16.png"))
    tool.write_theme_icons({(b"ics4", -3995): [bytes([1]) * 128],
                            (b"ics#", -3995): [b"\xff" * 64]}, [(9, palette)], out)
    assert _png_size(os.path.join(icons, "disk-16.png")) == (16, 16)
    assert _png_size(os.path.join(icons, "disk-32.png")) == (32, 32), \
        "the System's small hard disk scales to the desktop"
    print("ok    the System file's 16px hard disk fills both sizes and stale icons clear")


def test_resource_map_bounds():
    # A corrupt map can name thousands of types that all read the same
    # 100 KB body: without a byte budget the parser copies it until memory
    # runs out (an AppleDouble header once did this on a 365 KB file).
    body = struct.pack(">I", 100000) + b"\x00" * 100000
    data_offset = 256
    data_length = len(body)
    map_offset = data_offset + data_length
    types = 3000
    type_list = struct.pack(">H", types - 1)
    shared_ref = 2 + types * 8
    for _ in range(types):
        type_list += struct.pack(">4sHH", b"icl8", 0, shared_ref)
    type_list += struct.pack(">hH", 0, 0xFFFF) + b"\x00" + struct.pack(">I", 0)[1:] + struct.pack(">I", 0)
    map_length = 28 + len(type_list)
    data = struct.pack(">IIII", data_offset, map_offset, data_length, map_length)
    data += b"\x00" * (data_offset - 16) + body
    data += struct.pack(">IHH", 0, 0, 0) + struct.pack(">HH", 28, 0) + type_list
    items = tool.parse_resource_fork(data)
    assert len(items) < types, "the map was not followed to the end"
    assert sum(len(b) for _t, _i, b in items) <= 64 * 1024 * 1024, "byte budget enforced"
    print("ok    a corrupt resource map cannot copy one body without bound")


test_theme_icons_scale_and_clear_stale()
test_resource_map_bounds()
print("OK")
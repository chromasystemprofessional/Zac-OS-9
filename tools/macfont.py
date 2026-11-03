#!/usr/bin/env python3
"""Mac OS 9 font resources, bitmap and TrueType alike.

A family is a FOND resource named by the font; its strikes are NFNT resources
numbered family ID * 128 + size. A strike holds one size's glyphs as a bitmap
strip, with a location table (where each glyph starts) and an offset/width
table. This turns a strike into the rows of a ZacOS .pfont font.

A suitcase also holds sfnt resources, each a raw TrueType/OpenType font: the
bytes install verbatim as .ttf (sanitised for modern shapers), and can be
rasterised back into .pfont strikes. The family and style come from the sfnt's
own `name` table, so a suitcase with several sfnts (regular, bold, italic, …)
yields one file per face.

Nothing from Apple ships: this only reads the user's own fonts on their machine.
"""

import struct


def resources(fork):
    """Return {type: {id: (name, body)}} for every resource in a fork."""
    data_off, map_off, data_len, map_len = struct.unpack(">IIII", fork[:16])
    rmap = fork[map_off:map_off + map_len]
    type_list = struct.unpack(">H", rmap[24:26])[0]
    name_list = struct.unpack(">H", rmap[26:28])[0]
    count = struct.unpack(">H", rmap[type_list:type_list + 2])[0] + 1
    out = {}
    for i in range(count):
        entry = rmap[type_list + 2 + 8 * i:type_list + 10 + 8 * i]
        rtype = entry[:4].decode("latin1")
        refs = struct.unpack(">H", entry[4:6])[0] + 1
        ref_off = type_list + struct.unpack(">H", entry[6:8])[0]
        bucket = out.setdefault(rtype, {})
        for j in range(refs):
            ref = rmap[ref_off + 12 * j:ref_off + 12 * j + 12]
            rid, name_off = struct.unpack(">hH", ref[:4])
            if name_off == 0xFFFF:
                name = ""
            else:
                at = name_list + name_off
                n = rmap[at]
                name = rmap[at + 1:at + 1 + n].decode("mac_roman", "replace")
            data = data_off + int.from_bytes(ref[5:8], "big")
            size = struct.unpack(">I", fork[data:data + 4])[0]
            bucket[rid] = (name, fork[data + 4:data + 4 + size])
    return out


def families(res):
    """{family name: family ID} from the FOND resources."""
    out = {}
    for rid, (name, body) in res.get("FOND", {}).items():
        if name:
            out[name] = struct.unpack(">H", body[2:4])[0]
    return out


def strikes(res, name):
    """{(size, style): body} for a family, from its FOND association table."""
    fond = res.get("FOND", {})
    out = {}
    for rid, (fname, body) in fond.items():
        if fname != name:
            continue
        count = struct.unpack(">H", body[52:54])[0] + 1
        for k in range(count):
            size, style, sid = struct.unpack(">HHh", body[54 + 6 * k:60 + 6 * k])
            for kind in ("NFNT", "FONT"):
                if sid in res.get(kind, {}):
                    out[(size, style)] = res[kind][sid][1]
                    break
    return out


def strike_glyphs(body):
    """Decode an NFNT strike: {code: (advance, top_offset, rows)} with rows as
    lists of 0/1 per column, top relative to the baseline (negative = above).

    The offset/width table gives each glyph's pen advance (low byte) and its
    left bearing (high byte, columns of blank space before the ink). Rows are
    advance-wide: bearing blanks, then ink, then trailing blanks. A glyph with
    no ink (space) is all blank; a missing glyph (ow 0xFFFF) is skipped."""
    (_ftype, first, last, _wmax, _kmax, _ndesc, _frw, frh, owt, ascent,
     _descent, _leading, row_words) = struct.unpack(">13H", body[:26])
    strike_bytes = row_words * 2 * frh
    image = body[26:26 + strike_bytes]
    loc_at = 26 + strike_bytes
    count = last - first + 3
    loc = struct.unpack(">%dH" % count, body[loc_at:loc_at + 2 * count])
    ow_at = 16 + 2 * owt
    ow = struct.unpack(">%dH" % count, body[ow_at:ow_at + 2 * count])
    glyphs = {}
    for k in range(count - 1):
        code = first + k
        if code > last or code > 255:
            break
        if ow[k] == 0xFFFF:
            continue  # no glyph for this code
        advance = ow[k] & 0xFF
        if advance <= 0 or advance > 32:
            continue
        bearing = min((ow[k] >> 8) & 0xFF, advance)
        x0, x1 = loc[k], loc[k + 1]
        if x1 < x0:
            continue
        rows = []
        for y in range(frh):
            bits = [0] * advance
            for x in range(x0, min(x1, x0 + advance - bearing)):
                word = image[(y * row_words * 2) + (x // 16) * 2:(y * row_words * 2) + (x // 16) * 2 + 2]
                v = struct.unpack(">H", word)[0] if len(word) == 2 else 0
                bits[bearing + (x - x0)] = (v >> (15 - (x % 16))) & 1
            rows.append(bits)
        glyphs[code] = (advance, -ascent, rows)
    return glyphs


def pfont_text(name, size, body):
    """A strike as a .pfont file (tools/fonts/build_fonts.py's format)."""
    (_ftype, _first, _last, _wmax, _kmax, _ndesc, _frw, frh, _owt, ascent,
     _descent, _leading, _row_words) = struct.unpack(">13H", body[:26])
    lines = ["; Converted from the Mac OS 9 font %s, %d point." % (name, size),
             "font %s" % name, "height %d" % frh, "baseline %d" % ascent, ""]
    for code, (advance, top, rows) in sorted(strike_glyphs(body).items()):
        char = bytes((code,)).decode("mac_roman", "replace")
        if len(char) != 1 or ord(char) < 32:
            continue
        lines.append("char U+%04X %d %d" % (ord(char), advance, top))
        for row in rows:
            lines.append("".join("#" if bit else "." for bit in row))
        lines.append("")
    return "\n".join(lines) + "\n"


def scalable(res, name):
    """The TrueType (sfnt) body a family links at size 0, or None."""
    for rid, (fname, body) in res.get("FOND", {}).items():
        if fname != name:
            continue
        count = struct.unpack(">H", body[52:54])[0] + 1
        for k in range(count):
            size, style, sid = struct.unpack(">HHh", body[54 + 6 * k:60 + 6 * k])
            if size == 0 and style == 0 and sid in res.get("sfnt", {}):
                return res["sfnt"][sid][1]
    return None


def pfont_from_sfnt(name, size, sfnt):
    """Rasterise a TrueType font at `size` pixels into .pfont text, one bit per
    pixel, the baseline at the font's ascent. Codes 32-126 and 128-255 (Mac
    Roman) become glyphs; an advance wider than 32 pixels is skipped."""
    import io
    from PIL import Image, ImageDraw, ImageFont

    font = ImageFont.truetype(io.BytesIO(sfnt), size, layout_engine=ImageFont.Layout.BASIC)
    ascent, descent = font.getmetrics()
    height = ascent + descent
    lines = ["; Rasterised from the Mac OS 9 TrueType font %s, %d pixels." % (name, size),
             "font %s" % name, "height %d" % height, "baseline %d" % ascent, ""]
    for code in list(range(32, 127)) + list(range(128, 256)):
        char = bytes((code,)).decode("mac_roman", "replace")
        if len(char) != 1:
            continue
        advance = max(1, round(font.getlength(char)))
        if advance > 32:
            continue
        image = Image.new("1", (advance, height), 0)
        ImageDraw.Draw(image).text((0, ascent), char, font=font, fill=1, anchor="ls")
        lines.append("char U+%04X %d %d" % (ord(char), advance, -ascent))
        for y in range(height):
            lines.append("".join("#" if image.getpixel((x, y)) else "." for x in range(advance)))
        lines.append("")
    return "\n".join(lines) + "\n"


def is_sfnt(data):
    """Whether the resource body looks like a TrueType/OpenType font."""
    if len(data) < 12:
        return False
    # TrueType 1.0, OpenType, or the old 'true'/'typ1' tags.
    return data[:4] in (b"\x00\x01\x00\x00", b"OTTO", b"true", b"typ1")


def sanitize_sfnt(data):
    """Return installable sfnt bytes: the font with Apple's AAT layout
    tables (mort, morx) removed and checksums fixed.

    Shapers (HarfBuzz, and through it Qt and GTK programs) reject those
    tables' Mac OS 9-era contents, so programs would get no text at all;
    nothing visible changes by dropping them. Raises ValueError when the
    font is unusable."""
    if not is_sfnt(data):
        raise ValueError("not a font")
    num = struct.unpack(">H", data[4:6])[0]
    if num <= 0 or num > 64 or len(data) < 12 + 16 * num:
        raise ValueError("sfnt directory is invalid")
    tables = []
    for i in range(num):
        entry = data[12 + 16 * i:12 + 16 * i + 16]
        tag, _check, off, length = struct.unpack(">4sIII", entry)
        if tag in (b"mort", b"morx"):
            continue
        if off + length > len(data):
            raise ValueError("sfnt table runs past the end")
        tables.append((tag, data[off:off + length]))
    tables.sort()
    out = bytearray(struct.pack(">IHHHH", struct.unpack(">I", data[:4])[0],
        len(tables), 0, 0, 0))
    # searchRange/entrySelector/rangeShift for the new table count.
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
    # checkSumAdjustment makes the whole file sum to the magic value.
    # head.checkSumAdjustment is the third word of the head table.
    off = 12 + 16 * len(tables)
    for tag, body in tables:
        while off % 4:
            off += 1
        if tag == b"head":
            break
        off += len(body)
    else:
        raise ValueError("sfnt has no head table")
    out[off + 8:off + 12] = b"\x00\x00\x00\x00"
    total = 0
    padded = bytes(out) + b"\x00" * (-len(out) % 4)
    for i in range(0, len(padded), 4):
        total = (total + struct.unpack(">I", padded[i:i + 4])[0]) & 0xFFFFFFFF
    adjustment = (0xB1B0AFBA - total) & 0xFFFFFFFF
    out[off + 8:off + 12] = struct.pack(">I", adjustment)
    return bytes(out)


def _table_offset(data, tag):
    """Offset of a table in an sfnt directory, or None."""
    if len(data) < 12:
        return None
    num = struct.unpack(">H", data[4:6])[0]
    if num <= 0 or num > 64 or len(data) < 12 + 16 * num:
        return None
    for i in range(num):
        entry = data[12 + 16 * i:12 + 16 * i + 16]
        if entry[:4] == tag:
            off, _length = struct.unpack(">II", entry[8:16])
            return off
    return None


def parse_sfnt_names(data):
    """Return (family, subfamily) from the sfnt `name` table.

    Prefers the English Unicode names; falls back to Mac Roman. Raises
    ValueError when the font has no usable names."""
    off = _table_offset(data, b"name")
    if off is None or len(data) < off + 6:
        raise ValueError("sfnt has no name table")
    fmt, count, strings = struct.unpack(">HHH", data[off:off + 6])
    if fmt != 0 or count <= 0 or count > 100:
        raise ValueError("sfnt name table is not format 0")
    base = off + 6
    best = {}
    for i in range(count):
        if base + 12 > len(data):
            break
        platform, script, _lang, name_id, length, string_off = \
            struct.unpack(">HHHHHH", data[base:base + 12])
        base += 12
        start = off + strings + string_off
        raw = data[start:start + length]
        if name_id not in (1, 2) or not raw:
            continue
        if platform == 3 or (platform == 0 and script == 0):
            try:
                text = raw.decode("utf-16-be").strip()
            except UnicodeDecodeError:
                continue
        elif platform == 1 and script == 0:
            text = raw.decode("mac_roman", "replace").strip()
        else:
            continue
        # First English Unicode wins; Mac Roman fills gaps only.
        if name_id not in best or (platform == 3 and best[name_id][0] != 3):
            best[name_id] = (platform, text)
    if 1 not in best or not best[1][1]:
        raise ValueError("sfnt has no family name")
    family = best[1][1]
    subfamily = best[2][1] if 2 in best and best[2][1] else "Regular"
    return family, subfamily


def coverage(data):
    """The Unicode codepoints a sfnt maps, from its BMP cmap (format 4).

    Used to rasterize exactly the glyphs a Mac font has; anything else
    falls back per glyph at draw time. Raises ValueError when unreadable."""
    off = _table_offset(data, b"cmap")
    if off is None or len(data) < off + 4:
        raise ValueError("sfnt has no cmap table")
    _ver, num = struct.unpack(">HH", data[off:off + 4])
    if num <= 0 or num > 32:
        raise ValueError("sfnt cmap has no subtables")
    tables = {}
    for i in range(num):
        platform, script, suboff = struct.unpack(">HHI", data[off + 4 + 8 * i:off + 12 + 8 * i])
        tables.setdefault((platform, script), off + suboff)
    table = None
    for key in ((3, 1), (3, 0), (0, 3), (0, 4), (1, 0)):
        if key in tables:
            table = tables[key]
            break
    if table is None or len(data) < table + 6:
        raise ValueError("sfnt has no usable cmap")
    fmt = struct.unpack(">H", data[table:table + 2])[0]
    if fmt == 0:
        # Byte encoding table: 256 one-byte glyph ids.
        if len(data) < table + 262:
            raise ValueError("sfnt cmap format 0 is invalid")
        return set(i for i in range(256) if data[table + 6 + i])
    if fmt == 6:
        first, count = struct.unpack(">HH", data[table + 6:table + 10])
        if count > 2048 or len(data) < table + 10 + 2 * count:
            raise ValueError("sfnt cmap format 6 is invalid")
        ids = struct.unpack(">%dH" % count, data[table + 10:table + 10 + 2 * count])
        return set(code for code, gid in zip(range(first, first + count), ids) if gid)
    if fmt != 4:
        raise ValueError("unsupported cmap format %d" % fmt)
    segcount = struct.unpack(">H", data[table + 6:table + 8])[0] // 2
    if segcount <= 0 or segcount > 512:
        raise ValueError("sfnt cmap has no segments")
    base = table + 14
    ends = struct.unpack(">%dH" % segcount, data[base:base + 2 * segcount])
    base += 2 * segcount + 2
    starts = struct.unpack(">%dH" % segcount, data[base:base + 2 * segcount])
    base += 2 * segcount
    deltas = struct.unpack(">%dh" % segcount, data[base:base + 2 * segcount])
    base += 2 * segcount
    rangepos = base
    codepoints = set()
    for i in range(segcount):
        if starts[i] == 0xFFFF and ends[i] == 0xFFFF:
            continue
        for cp in range(starts[i], min(ends[i], 0xFFFF) + 1):
            ro = struct.unpack(">H", data[rangepos + 2 * i:rangepos + 2 * i + 2])[0]
            if ro == 0:
                gid = (cp + deltas[i]) & 0xFFFF
            else:
                at = rangepos + 2 * i + ro + (cp - starts[i]) * 2
                if at + 2 > len(data):
                    continue
                gid = struct.unpack(">H", data[at:at + 2])[0]
                if gid:
                    gid = (gid + deltas[i]) & 0xFFFF
            if gid:
                codepoints.add(cp)
    return codepoints


def rasterize_pfont(ttf_path, pixel_size):
    """Render a .pfont bitmap font (the format in assets/fonts/) from a
    TrueType face at `pixel_size` px, using PIL's basic layout (never the
    shaping engine, which Mac OS 9 tables can trip).

    Covers printable Latin-1 plus typographic punctuation the face has;
    anything else falls back per glyph at draw time. Returns the file
    text, or raises ValueError."""
    from PIL import ImageFont
    try:
        font = ImageFont.truetype(ttf_path, pixel_size, layout_engine=ImageFont.Layout.BASIC)
    except (IOError, OSError) as e:
        raise ValueError("cannot open font: %s" % e)
    ascent, descent = font.getmetrics()
    height = ascent + descent
    if height <= 0 or height > 256 or ascent <= 0:
        raise ValueError("font has no usable metrics")
    try:
        with open(ttf_path, "rb") as f:
            covered = coverage(f.read())
    except (OSError, ValueError) as e:
        raise ValueError("cannot read cmap: %s" % e)
    wanted = list(range(0x20, 0x7F)) + list(range(0xA0, 0x100))
    wanted += [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026]
    chars = sorted(cp for cp in wanted if cp in covered)
    if not chars:
        raise ValueError("font covers nothing usable")
    cap = ascent - font.getbbox("H")[1] if 0x48 in chars else ascent * 3 // 4
    xheight = ascent - font.getbbox("x")[1] if 0x78 in chars else ascent // 2
    out = ["font Rasterized", "height %d" % height, "baseline %d" % ascent,
           "cap %d" % cap, "xheight %d" % xheight, ""]
    for cp in chars:
        ch = chr(cp)
        advance = int(round(font.getlength(ch)))
        if not 0 < advance <= 32:
            # Too wide for the 32 px bitmap rows: leave it to the
            # fallback face rather than clipping ink.
            continue
        try:
            mask = font.getmask(ch)
            x0, y0, x1, y1 = font.getbbox(ch)
        except (IOError, OSError, ValueError):
            continue
        if mask.size == (0, 0) or x1 <= x0 or y1 <= y0:
            # Blank: only space earns an empty glyph (anything else would
            # hide the fallback face behind nothing).
            if ch != " ":
                continue
            out.append("char space %d 0" % advance)
            out.append("")
            continue
        rows = []
        for y in range(y1 - y0):
            rows.append("".join("#" if mask.getpixel((x, y)) >= 128 else "." for x in range(x1 - x0)))
        # Left bearing: ink right of the pen is offset; ink left of it
        # (a negative x0, from rounding) lands at the pen so no ink is
        # lost. Either way every row ends up exactly the pen advance
        # wide, the contract tools/fonts/build_fonts.py checks.
        prefix = "." * x0 if x0 > 0 else ""
        rows = [prefix + r for r in rows]
        maxw = max(len(r) for r in rows)
        if maxw > advance:
            advance = maxw
            if advance > 32:
                continue
        rows = [r.ljust(advance, ".") for r in rows]
        top = y0 - ascent
        if top < -ascent or top + len(rows) - 1 > height - ascent - 1:
            # Accented capitals can rise above the ascent: leave them
            # to the fallback face (as build_fonts.py does) rather
            # than clipping ink.
            continue
        key = "space" if ch == " " else ch
        out.append("char %s %d %d" % (key, advance, top))
        out.extend(rows)
        out.append("")
    return "\n".join(out)


def ttf_filename(family, subfamily, taken):
    """File stem for a face, made unique within `taken` (a set, updated)."""
    safe = "".join(c if c.isalnum() or c in ("-", "_") else "_" for c in family).strip("_")
    safe = safe or "MacFont"
    stem = safe if subfamily in ("Regular", "Normal") else "%s-%s" % (safe, "".join(
        c if c.isalnum() else "" for c in subfamily))
    name = stem
    n = 1
    while name.lower() + ".ttf" in taken:
        n += 1
        name = "%s_%d" % (stem, n)
    taken.add(name.lower() + ".ttf")
    return name + ".ttf"

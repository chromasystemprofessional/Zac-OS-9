#!/usr/bin/env python3
"""Read Mac OS 9 bitmap fonts: FOND families and NFNT strikes.

A family is a FOND resource named by the font; its strikes are NFNT resources
numbered family ID * 128 + size. A strike holds one size's glyphs as a bitmap
strip, with a location table (where each glyph starts) and an offset/width
table. This turns a strike into the rows of a ZacOS .pfont font.

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
    lists of 0/1 per column, top relative to the baseline (negative = above)."""
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
        if code > last:
            break
        if ow[k] == 0xFFFF:
            continue  # no glyph for this code
        x0, x1 = loc[k], loc[k + 1]
        if x1 <= x0 or x1 - x0 > 32:
            continue
        rows = []
        for y in range(frh):
            row_start = y * row_words * 16
            bits = []
            for x in range(x0, x1):
                word = image[(y * row_words * 2) + (x // 16) * 2:(y * row_words * 2) + (x // 16) * 2 + 2]
                v = struct.unpack(">H", word)[0] if len(word) == 2 else 0
                bits.append((v >> (15 - (x % 16))) & 1)
            rows.append(bits)
        glyphs[code] = (x1 - x0, -ascent, rows)
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

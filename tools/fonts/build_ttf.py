"""Build the ZacOS 9 fonts as TrueType (assets/fonts/*.ttf) from the .pfont
sources, so that programs can set text in them too (the Fonts folder lists
them; share/fontconfig/zacos9-fonts.conf lets programs see them).

usage: build_ttf.py [--check]

Each pixel becomes a square of the outline, 100 units on a side, in an em
of 100 units per pixel of the design size (12 px for System, 9 px for
Views), so that at that size, with hinting off, every edge lands on a
pixel boundary and the font renders exactly as the bitmap does; at other
sizes it scales as a pixel font would. The Views italic is the synthesised
one the Finder uses for alias names (build_fonts.italic_shift).

--check fails if the .ttf files are out of date (run from the test suite);
exits 77 (skipped) without fontTools.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_fonts import FONTS, ROOT, Font, italic_shift  # noqa: E402

PX = 100  # units per pixel
OUT_DIR = os.path.join(ROOT, "assets", "fonts")
# (font key, file, italic, design size in px)
FACES = [
    ("system", "ZacOS9System-Regular.ttf", False, 12),
    ("views", "ZacOS9Views-Regular.ttf", False, 9),
    ("views", "ZacOS9Views-Italic.ttf", True, 9),
]
VERSION = "1.000"
DATE = "Tue Oct 20 12:00:00 2026"


def glyph_name(cp):
    from fontTools.agl import UV2AGL
    return "space" if cp == 32 else UV2AGL.get(cp, f"uni{cp:04X}")


def runs(glyph, italic):
    """The glyph's ink as (x0, x1, y0, y1) in pixels, y up from the
    baseline: one rectangle per run of pixels in a row."""
    out = []
    for i, row in enumerate(glyph.rows):
        y = glyph.top + i
        dx = italic_shift(y) if italic else 0
        j = 0
        while j < len(row):
            if row[j] != "#":
                j += 1
                continue
            k = j
            while k < len(row) and row[k] == "#":
                k += 1
            out.append((j + dx, k + dx, -y, -y + 1))
            j = k
    return out


def rectangle(pen, x0, x1, y0, y1, hole=False):
    """A pixel-unit rectangle, clockwise (TrueType's outside), or
    counter-clockwise for a hole."""
    points = [(x0, y0), (x0, y1), (x1, y1), (x1, y0)]
    if hole:
        points.reverse()
    pen.moveTo((points[0][0] * PX, points[0][1] * PX))
    for x, y in points[1:]:
        pen.lineTo((x * PX, y * PX))
    pen.closePath()


def build(font, italic, size):
    from fontTools.fontBuilder import FontBuilder
    from fontTools.misc.timeTools import timestampFromString
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.ttLib import newTable

    family = font.meta["name"]
    style = "Italic" if italic else "Regular"
    cap, xheight = font.meta["cap"], font.meta["xheight"]
    ascent = (font.meta["baseline"] + 1) * PX
    descent = -(font.meta["height"] - font.meta["baseline"] - 1) * PX
    upem = size * PX

    order = [".notdef"] + [glyph_name(cp) for cp in sorted(font.glyphs)]
    glyphs, metrics, cmap = {}, {}, {}
    pen = TTGlyphPen(None)
    rectangle(pen, 1, 6, 0, cap)
    rectangle(pen, 2, 5, 1, cap - 1, hole=True)
    glyphs[".notdef"] = pen.glyph()
    metrics[".notdef"] = (7 * PX, PX)
    for cp in sorted(font.glyphs):
        g = font.glyphs[cp]
        name = glyph_name(cp)
        pen = TTGlyphPen(None)
        ink = runs(g, italic)
        for x0, x1, y0, y1 in ink:
            rectangle(pen, x0, x1, y0, y1)
        glyphs[name] = pen.glyph()
        lsb = min(r[0] for r in ink) * PX if ink else 0
        metrics[name] = (g.advance * PX, lsb)
        cmap[cp] = name

    fb = FontBuilder(upem, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=ascent, descent=descent, lineGap=0,
                             caretSlopeRise=2 if italic else 1, caretSlopeRun=1 if italic else 0)
    ps_name = family.replace(" ", "") + "-" + style
    fb.setupNameTable({
        "familyName": family,
        "styleName": style,
        "uniqueFontIdentifier": f"{ps_name};{VERSION}",
        "fullName": family if style == "Regular" else f"{family} {style}",
        "psName": ps_name,
        "version": f"Version {VERSION}",
        "copyright": "Copyright 2026 the ZacOS 9 project",
        "manufacturer": "ZacOS 9",
        "description": f"{family}: an original pixel font drawn for ZacOS 9, "
                       f"made for {size} px. Built from assets/fonts by tools/fonts/build_ttf.py.",
        "licenseDescription": "GNU General Public License version 3 or later",
        "licenseInfoURL": "https://www.gnu.org/licenses/gpl-3.0.html",
    })
    fb.setupOS2(
        sTypoAscender=ascent, sTypoDescender=descent, sTypoLineGap=0,
        usWinAscent=ascent, usWinDescent=-descent,
        sxHeight=xheight * PX, sCapHeight=cap * PX,
        version=4, usWeightClass=400, usWidthClass=5, fsType=0,
        # USE_TYPO_METRICS (bit 7), and REGULAR (6) or ITALIC (0).
        fsSelection=(1 << 7) | ((1 << 0) if italic else (1 << 6)),
        xAvgCharWidth=round(sum(m[0] for m in metrics.values()) / len(metrics)),
        achVendID="ZAC9",
    )
    fb.font["OS/2"].recalcUnicodeRanges(fb.font)
    fb.setupPost(italicAngle=-26.565 if italic else 0.0,
                 underlinePosition=-PX, underlineThickness=PX, isFixedPitch=0)
    # The baseline at y=0, and ppem forced to integers (a pixel font).
    stamp = timestampFromString(DATE)
    fb.setupHead(unitsPerEm=upem, created=stamp, modified=stamp, lowestRecPPEM=size,
                 flags=(1 << 0) | (1 << 3), macStyle=(1 << 1) if italic else 0)
    gasp = newTable("gasp")
    gasp.version = 1
    gasp.gaspRange = {0xFFFF: 0x000A}  # grayscale, symmetric smoothing; no grid fitting
    fb.font["gasp"] = gasp
    return fb.font


def describe(ttfont):
    """What matters of a font, to tell a stale file from a fresh one."""
    glyf, hmtx = ttfont["glyf"], ttfont["hmtx"]
    outlines = {}
    for name in ttfont.getGlyphOrder():
        coords, ends, flags = glyf[name].getCoordinates(glyf)
        outlines[name] = (list(map(tuple, coords)), list(ends), hmtx[name])
    names = {(r.nameID, r.platformID): r.toUnicode() for r in ttfont["name"].names}
    hhea, os2 = ttfont["hhea"], ttfont["OS/2"]
    return (outlines, ttfont.getBestCmap(), names, ttfont["head"].unitsPerEm,
            (hhea.ascent, hhea.descent, hhea.lineGap),
            (os2.sTypoAscender, os2.sTypoDescender, os2.usWinAscent, os2.usWinDescent, os2.fsSelection))


def main():
    try:
        import fontTools  # noqa: F401
        from fontTools.ttLib import TTFont
    except ImportError:
        print("skip: fontTools (python3-fonttools) isn't installed")
        sys.exit(77)
    fonts = {key: Font(os.path.join(ROOT, "assets", "fonts", name)) for key, name in FONTS}
    for key, file, italic, size in FACES:
        path = os.path.join(OUT_DIR, file)
        fresh = build(fonts[key], italic, size)
        if sys.argv[1:] == ["--check"]:
            if not os.path.exists(path) or describe(TTFont(path)) != describe(fresh):
                sys.exit(f"assets/fonts/{file} is out of date: run tools/fonts/build_ttf.py")
            continue
        fresh.save(path)
        print(f"{file}: {len(fresh.getGlyphOrder())} glyphs, {os.path.getsize(path)} bytes")


if __name__ == "__main__":
    main()

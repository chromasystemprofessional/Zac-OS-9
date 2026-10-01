"""Compile the Platinum bitmap fonts (assets/fonts/ (*.pfont)) into C.

usage: build_fonts.py [--specimen OUT.png | --check]

Writes lib/fonts_data.h. Accented Latin letters are composed from a base
letter and the font's marks, centred over the base's ink. With
--specimen, also renders every font (and the synthesised italic) to a PNG
at 3x for review.
"""
import os
import sys
import unicodedata

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
FONTS = [
    ("system", "platinum-system-12.pfont"),
    ("views", "platinum-views-9.pfont"),
]
# Combining marks we can draw, by Unicode decomposition.
MARKS = {
    "0300": "grave", "0301": "acute", "0302": "circumflex", "0303": "tilde",
    "0308": "diaeresis", "030A": "ring", "0327": "cedilla",
}


class Glyph:
    def __init__(self, cp, advance, top, rows):
        self.cp, self.advance, self.top, self.rows = cp, advance, top, rows

    def ink_cols(self):
        cols = [x for r in self.rows for x, c in enumerate(r) if c == "#"]
        return (min(cols), max(cols)) if cols else None


class Font:
    def __init__(self, path):
        self.path = path
        self.glyphs, self.marks, self.meta = {}, {}, {}
        self.parse()
        self.compose()
        self.check()

    def fail(self, line_no, msg):
        sys.exit(f"{self.path}:{line_no}: {msg}")

    def parse(self):
        cur = None  # (kind, key, advance, top, rows, line_no)

        def flush():
            if not cur:
                return
            kind, key, advance, top, rows, line_no = cur
            if kind == "char":
                for r in rows:
                    if len(r) != advance:
                        self.fail(line_no, f"{chr(key)!r}: row {r!r} is not {advance} wide")
                if key in self.glyphs:
                    self.fail(line_no, f"{chr(key)!r} defined twice")
                self.glyphs[key] = Glyph(key, advance, top, rows)
            else:
                widths = {len(r) for r in rows}
                if len(widths) != 1:
                    self.fail(line_no, f"mark {key}: ragged rows")
                self.marks[key] = rows

        with open(self.path, encoding="utf-8") as f:
            for n, raw in enumerate(f, 1):
                line = raw.rstrip("\n")
                if not line.strip() or line.startswith(";"):
                    continue
                if set(line) <= {"#", "."}:
                    if not cur:
                        self.fail(n, "pixel row outside a glyph")
                    cur[4].append(line)
                    continue
                words = line.split()
                if words[0] == "char":
                    flush()
                    if len(words) != 4:
                        self.fail(n, "expected: char C ADVANCE TOP")
                    c = words[1]
                    cp = 32 if c == "space" else int(c[2:], 16) if c.startswith("U+") else ord(c)
                    cur = ("char", cp, int(words[2]), int(words[3]), [], n)
                elif words[0] == "mark":
                    flush()
                    cur = ("mark", words[1], 0, 0, [], n)
                elif words[0] == "font":
                    flush()
                    cur = None
                    self.meta["name"] = " ".join(words[1:])
                else:
                    flush()
                    cur = None
                    self.meta[words[0]] = int(words[1])
            flush()

    def compose(self):
        gap = self.meta.get("accent-gap", 1)
        for cp in range(0xC0, 0x180):
            if cp in self.glyphs:
                continue
            parts = unicodedata.decomposition(chr(cp)).split()
            if len(parts) != 2 or parts[1] not in MARKS or MARKS[parts[1]] not in self.marks:
                continue
            base_cp = int(parts[0], 16)
            if base_cp == ord("i"):
                base_cp = 0x131  # dotless i
            base = self.glyphs.get(base_cp)
            if not base:
                continue
            mark = self.marks[MARKS[parts[1]]]
            mw, mh = len(mark[0]), len(mark)
            ink = base.ink_cols()
            # A letter narrower than its accent (ï) widens to fit it.
            pad = max(0, mw - (ink[1] - ink[0] + 1))
            lp, rp = pad // 2, pad - pad // 2
            base = Glyph(base.cp, base.advance + pad, base.top,
                         ["." * lp + r + "." * rp for r in base.rows])
            ink = base.ink_cols()
            mx = (ink[0] + ink[1] + 1 - mw) // 2
            mx = max(0, min(mx, base.advance - mw))
            rows = {base.top + i: list(r) for i, r in enumerate(base.rows)}
            bottom = base.top + len(base.rows) - 1
            if MARKS[parts[1]] == "cedilla":
                y0 = bottom + 1 if bottom <= 0 else bottom + 1
                y0 = max(1, y0)
            else:
                y0 = base.top - gap - mh
            for i, r in enumerate(mark):
                y = y0 + i
                row = rows.setdefault(y, ["."] * base.advance)
                for j, c in enumerate(r):
                    if c == "#":
                        row[mx + j] = "#"
            top = min(rows)
            if top < -self.meta["baseline"] or max(rows) > self.meta["height"] - self.meta["baseline"] - 1:
                continue  # no room (a cedilla under a descender): left to the fallback
            out = ["".join(rows.get(y, ["."] * base.advance)) for y in range(top, max(rows) + 1)]
            self.glyphs[cp] = Glyph(cp, base.advance, top, out)

    def check(self):
        h, b = self.meta["height"], self.meta["baseline"]
        for g in self.glyphs.values():
            if g.top < -b or g.top + len(g.rows) - 1 > h - b - 1:
                sys.exit(f"{self.path}: {chr(g.cp)!r} doesn't fit the {h} px line box")
            if g.advance > 16:
                sys.exit(f"{self.path}: {chr(g.cp)!r} is wider than 16 px")


def emit(fonts, out):
    lines = [
        "/* Generated by tools/fonts/build_fonts.py from assets/fonts/ (*.pfont).",
        " * Do not edit; edit the .pfont sources and regenerate. */",
        "",
    ]
    for key, font in fonts:
        rows, glyphs = [], []
        for cp in sorted(font.glyphs):
            g = font.glyphs[cp]
            first = len(rows)
            for r in g.rows:
                rows.append(sum(1 << i for i, c in enumerate(r) if c == "#"))
            glyphs.append(f"\t{{ 0x{cp:04X}, {g.top}, {g.advance}, {len(g.rows)}, {first} }},")
        lines.append(f"static const uint16_t font_{key}_rows[] = {{")
        for i in range(0, len(rows), 12):
            lines.append("\t" + ", ".join(f"0x{v:04x}" for v in rows[i:i + 12]) + ",")
        lines.append("};")
        lines.append(f"static const struct pl_glyph font_{key}_glyphs[] = {{")
        lines.extend(glyphs)
        lines.append("};")
        lines.append(f"static const struct pl_bitmap_font font_{key} = {{")
        lines.append(f"\t\"{font.meta['name']}\", {font.meta['height']}, {font.meta['baseline']},")
        lines.append(f"\tfont_{key}_glyphs, {len(glyphs)}, font_{key}_rows,")
        lines.append("};")
        lines.append("")
    with open(out, "w") as f:
        f.write("\n".join(lines))


def italic_shift(y):
    """QuickDraw-style italic: one pixel right per two rows above the baseline."""
    return (-y) // 2 if y <= 0 else -((y + 1) // 2)


def specimen(fonts, out):
    from PIL import Image, ImageDraw
    samples = [
        "File  Edit  View  Special  Help",
        "Show Clipboard   Preferences...   Get Info",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        "abcdefghijklmnopqrstuvwxyz",
        "0123456789  !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~",
        "… – — ‘’ “” • ° © ® ™ ⌫",
        "ÀÁÂÃÄÅÇÈÉÊËÑÖÜ "
        "àáâãäåçèéêëìíñöüÿ",
        "2 items, 494 MB available   External   Promotion",
    ]
    blocks = []
    for key, font in fonts:
        for italic in (False, True) if key == "views" else (False,):
            blocks.append((font, italic))
    lh = max(f.meta["height"] for _, f in fonts) + 2
    width = 520
    img = Image.new("RGB", (width, 8 + lh * len(samples) * len(blocks) + 12 * len(blocks)), "white")
    px = img.load()
    y0 = 4
    for font, italic in blocks:
        for text in samples:
            x = 4
            base = y0 + font.meta["baseline"]
            for ch in text:
                g = font.glyphs.get(ord(ch))
                if not g:
                    x += 4
                    continue
                for i, r in enumerate(g.rows):
                    y = g.top + i
                    dx = italic_shift(y) if italic else 0
                    for j, c in enumerate(r):
                        if c == "#" and 0 <= x + j + dx < width:
                            px[x + j + dx, base + y] = (0, 0, 0)
                x += g.advance
            y0 += lh
        ImageDraw.Draw(img).line([(0, y0 + 4), (width, y0 + 4)], fill=(200, 200, 200))
        y0 += 12
    img.resize((img.width * 3, img.height * 3), Image.NEAREST).save(out)


def main():
    fonts = [(key, Font(os.path.join(ROOT, "assets", "fonts", name))) for key, name in FONTS]
    out = os.path.join(ROOT, "lib", "fonts_data.h")
    if sys.argv[1:] == ["--check"]:
        # Fail if lib/fonts_data.h is stale (run from the test suite).
        import tempfile
        with tempfile.NamedTemporaryFile("r", suffix=".h") as tmp:
            emit(fonts, tmp.name)
            with open(out) as f:
                if f.read() != tmp.read():
                    sys.exit("lib/fonts_data.h is out of date: run tools/fonts/build_fonts.py")
        return
    emit(fonts, out)
    for key, font in fonts:
        print(f"{font.meta['name']}: {len(font.glyphs)} glyphs")
    if len(sys.argv) == 3 and sys.argv[1] == "--specimen":
        specimen(fonts, sys.argv[2])


if __name__ == "__main__":
    main()

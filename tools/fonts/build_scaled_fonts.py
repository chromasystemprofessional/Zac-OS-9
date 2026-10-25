"""Write first versions of the ZacOS 9 fonts for 1.25x, 1.5x and 1.75x
screens (assets/fonts/platinum-*-125|150|175.pfont) from the 1x ones.

usage: build_scaled_fonts.py [--force]

At those scales 1x pixel art can't be enlarged evenly (four pixels onto
five at 1.25x), and text suffers most: stems one pixel here, two there.
So each size gets a bitmap font of its own, drawn at the screen's pixels
(lib/text.c text_render_scaled), and these are its first drafts, made so:

- a stem keeps one thickness for its 1x width at a scale (halves rounded
  down, so no bolder than it must be), and so do horizontal strokes; a
  serif or bar beside a stem takes any extra, not the stem;
- the extra pixels go to the blank columns between strokes, and to the
  rows without horizontal strokes;
- the font's lines (cap height, x-height, baseline, descender) are where
  scaling puts them, so the letters keep their proportions;
- a letter's width is its 1x one scaled and rounded down (text is never
  wider than the 1x layout allows), but every blank run keeps a pixel.

They are ordinary .pfont files, meant to be touched up by hand: the
generated drafts are only a start. So existing files are left alone
unless --force is given. tools/fonts/build_fonts.py compiles them.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_fonts import ROOT, Font  # noqa: E402

FONT_DIR = os.path.join(ROOT, "assets", "fonts")
SOURCES = [("platinum-system-12", "System"), ("platinum-views-9", "Views")]
SCALES = [(1.25, "125"), (1.5, "150"), (1.75, "175")]


def round_up(v):
    return int(math.floor(v + 0.5))


def round_down(v):
    return int(math.ceil(v - 0.5))


def row_widths(font, s):
    """Each line-box row's thickness at scale s."""
    m = font.meta
    height, base = m["height"], m["baseline"]
    score = [0] * height  # letters with a horizontal stroke (3+ px) in the row
    for g in font.glyphs.values():
        for i, r in enumerate(g.rows):
            y = base + g.top + i
            run = best = 0
            for c in r:
                run = run + 1 if c == "#" else 0
                best = max(best, run)
            if best >= 3:
                score[y] += 1
    stroke = [n >= 5 for n in score]
    t = max(1, round_down(s))
    anchors = sorted({0, base - m["cap"] + 1, base - m["xheight"] + 1, base + 1, height})
    widths = [1] * height
    for a, b in zip(anchors, anchors[1:]):
        total = round_up(b * s) - round_up(a * s)
        rows = list(range(a, b))
        fixed = [y for y in rows if stroke[y]]
        free = [y for y in rows if not stroke[y]]
        rest = total - t * len(fixed)
        if rest < len(free) or (not free and rest):
            for y in rows:  # strokes can't all be even here: plain scaling
                widths[y] = round_up((y + 1) * s) - round_up(y * s)
            continue
        for y in fixed:
            widths[y] = t
        for j, y in enumerate(free):
            widths[y] = round_up(rest * (j + 1) / len(free)) - round_up(rest * j / len(free))
    return widths


def column_widths(rows, run, n, total, s):
    """Widths of n inked columns from `run` sharing `total` pixels: spread
    evenly, but each stem (side-by-side columns each an unbroken vertical
    stroke of 2 rows or more) its 1x width scaled, halves rounded down,
    the difference taken from the columns beside it (serifs, bars)."""
    def longest(x):
        best = cur = 0
        for r in rows:
            cur = cur + 1 if r[x] == "#" else 0
            best = max(best, cur)
        return best
    stem = [longest(x) >= 2 for x in range(run, run + n)]
    groups, i = [], 0
    while i < n:
        if stem[i]:
            j = i
            while j < n and stem[j]:
                j += 1
            groups.append((i, j))
            i = j
        else:
            i += 1
    want = {g: max(g[1] - g[0], round_down((g[1] - g[0]) * s)) for g in groups}
    if total <= 0:  # only what the letter needs
        w = [1] * n
        for (a, b), t in want.items():
            for k in range(a, b):
                w[k] = round_up(t * (k - a + 1) / (b - a)) - round_up(t * (k - a) / (b - a))
        return w
    w = [round_up(total * (i + 1) / n) - round_up(total * i / n) for i in range(n)]
    for (a, b), t in want.items():
        diff = sum(w[a:b]) - t
        for k in range(a, b):
            w[k] = round_up(t * (k - a + 1) / (b - a)) - round_up(t * (k - a) / (b - a))
        # The difference to the nearest columns that aren't stems.
        for d in range(1, n):
            for j in (a - d, b - 1 + d):
                if diff and 0 <= j < n and not stem[j] and w[j] + diff >= 1:
                    w[j] += diff
                    diff = 0
        k = 0
        while diff and k < 4 * n:  # none: the stems share it, left first
            j = k % n
            step = 1 if diff > 0 else -1
            if stem[j] and w[j] + step >= 1:
                w[j] += step
                diff -= step
            k += 1
    return w


def column_edges(rows, advance, s):
    """A letter's column edges at scale s, and its width."""
    ink = [any(r[x] == "#" for r in rows) for x in range(advance)]
    runs = []
    for x in range(advance):
        if runs and runs[-1][0] == ink[x]:
            runs[-1][1] += 1
        else:
            runs.append([ink[x], 1])
    width = int(math.floor(advance * s))
    w = []
    x = 0
    for inked, n in runs:
        if inked:  # at least each column's own width (column_widths)
            need = sum(column_widths(rows, x, n, 0, s))
            w.append(max(round_down(n * s), need))
        else:
            w.append(1)
        x += n
    rest = width - sum(w)
    if rest < 0:
        width, rest = sum(w), 0  # can't be narrower and stay itself
    blanks = [i for i, (inked, _) in enumerate(runs) if not inked]
    if blanks and rest:
        total = sum(runs[i][1] for i in blanks)
        shares = [(rest * runs[i][1] / total, i) for i in blanks]
        for v, i in shares:
            w[i] += int(v)
        left = rest - sum(int(v) for v, _ in shares)
        for v, i in sorted(shares, key=lambda t: (-(t[0] - int(t[0])), t[1]))[:left]:
            w[i] += 1
    elif rest:
        w[-1] += rest
    edges, x = [0], 0
    for (inked, n), size in zip(runs, w):
        if inked:
            for cw in column_widths(rows, x, n, size, s):
                edges.append(edges[-1] + cw)
        else:
            start = edges[-1]
            for j in range(1, n + 1):
                edges.append(start + round_up(size * j / n))
        x += n
    return edges, width


def scaled(font, s, name, source):
    m = font.meta
    widths = row_widths(font, s)
    y_edges = [0]
    for w in widths:
        y_edges.append(y_edges[-1] + w)
    base = m["baseline"]
    lines = [
        f"; {m['name']} at {s}x: the font for {s}x screens (see",
        f"; tools/fonts/build_scaled_fonts.py). Drafted by that script from",
        f"; {source}.pfont, and meant to be touched up by hand: it isn't",
        "; regenerated over (but with --force). Same format as the 1x font;",
        "; every glyph drawn at the screen's pixels.",
        "",
        f"font {name}",
        f"height {y_edges[-1]}",
        f"baseline {y_edges[base]}",
        f"cap {y_edges[base + 1] - y_edges[base + 1 - m['cap']]}",
        f"xheight {y_edges[base + 1] - y_edges[base + 1 - m['xheight']]}",
        "",
    ]
    for cp in sorted(font.glyphs):
        g = font.glyphs[cp]
        edges, width = column_edges(g.rows, g.advance, s)
        top = y_edges[base + g.top] - y_edges[base]
        out = []
        for i, r in enumerate(g.rows):
            y = base + g.top + i
            row = ["."] * width
            for x, c in enumerate(r):
                if c == "#":
                    for dx in range(edges[x], min(edges[x + 1], width)):
                        row[dx] = "#"
            out.extend(["".join(row)] * widths[y])
        label = "space" if cp == 32 else chr(cp) if 33 <= cp < 127 else f"U+{cp:04X}"
        lines.append(f"char {label} {width} {top}")
        lines.extend(out)
        lines.append("")
    return "\n".join(lines)


def main():
    force = sys.argv[1:] == ["--force"]
    for source, family in SOURCES:
        font = Font(os.path.join(FONT_DIR, source + ".pfont"))
        for s, suffix in SCALES:
            path = os.path.join(FONT_DIR, f"{source}-{suffix}.pfont")
            if os.path.exists(path) and not force:
                print(f"{path}: kept (--force to draft it again)")
                continue
            with open(path, "w", encoding="utf-8") as f:
                f.write(scaled(font, s, f"{font.meta['name']} {s}x", source))
            print(f"{path}: drafted")


if __name__ == "__main__":
    main()

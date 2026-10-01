"""Per-character metrics of a line of text in a HIG figure.

usage: glyphs.py NAME X0 X1 Y0 Y1 INK "Text"

Columns X0..X1 of rows Y0..Y1 are scanned for pixels of the ink color
(hex, e.g. 000000). Runs of ink columns are matched, in order, to the
non-space characters of Text. For each character this prints the ink
width and the distance from its first ink column to the next
character's (its advance, if the next glyph starts flush), plus the ink's
top and bottom rows relative to the baseline row (the lowest row of the
first character's ink unless it has a descender: pass BASELINE=row in
the environment to set it).

This measures spacing only; glyph shapes are drawn from scratch.
"""
import os
import sys
from PIL import Image

name = sys.argv[1]
x0, x1, y0, y1 = map(int, sys.argv[2:6])
ink = tuple(int(sys.argv[6][i:i + 2], 16) for i in (0, 2, 4))
text = sys.argv[7]
im = Image.open(f"figs/{name}.png").convert("RGB")


def is_ink(x, y):
    return im.getpixel((x, y)) == ink


cols = [x for x in range(x0, x1 + 1) if any(is_ink(x, y) for y in range(y0, y1 + 1))]
runs = []
for x in cols:
    if runs and x == runs[-1][1] + 1:
        runs[-1][1] = x
    else:
        runs.append([x, x])

chars = [c for c in text if c != " "]
if len(runs) != len(chars):
    print(f"warning: {len(runs)} ink runs for {len(chars)} characters", file=sys.stderr)

baseline = int(os.environ.get("BASELINE", "-1"))
rows = []
for (l, r) in runs:
    ys = [y for y in range(y0, y1 + 1) for x in range(l, r + 1) if is_ink(x, y)]
    rows.append((min(ys), max(ys)))
if baseline < 0:
    baseline = rows[0][1]

# Which runs are preceded by a space in the text.
gap_before = []
i = 0
prev_space = False
for c in text:
    if c == " ":
        prev_space = True
        continue
    gap_before.append(prev_space)
    prev_space = False
    i += 1

print(f"baseline row {baseline}")
for k, ((l, r), (top, bot)) in enumerate(zip(runs, rows)):
    c = chars[k] if k < len(chars) else "?"
    nxt = runs[k + 1][0] - l if k + 1 < len(runs) else None
    space = " (then space)" if k + 1 < len(gap_before) and gap_before[k + 1] else ""
    print(f"{c!r:6} ink {r - l + 1:2}  next+{nxt if nxt is not None else '-':>3}{space:13}"
          f"  rows {top - baseline:+d}..{bot - baseline:+d}")

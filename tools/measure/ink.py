"""Report the horizontal ink extent of each text row band in a region.

usage: ink.py NAME X0 X1 Y0 Y1 [BG]
Scans rows Y0..Y1 between columns X0..X1; any pixel that isn't the
background gray (hex nibble, default 'd' = #DDDDDD) or the menu's white/shade
bevel colors counts as ink. Prints one line per contiguous band of rows.
"""
import sys
from PIL import Image

name = sys.argv[1]
x0, x1, y0, y1 = map(int, sys.argv[2:6])
bg = int(sys.argv[6], 16) * 0x11 if len(sys.argv) > 6 else 0xDD
im = Image.open(f"figs/{name}.png").convert("RGB")


def ink_cols(y):
    cols = []
    for x in range(x0, x1 + 1):
        r, g, b = im.getpixel((x, y))
        if (r, g, b) != (bg, bg, bg):
            cols.append(x)
    return cols


band = None
for y in list(range(y0, y1 + 1)) + [None]:
    cols = ink_cols(y) if y is not None else []
    if cols:
        if band is None:
            band = [y, y, min(cols), max(cols)]
        else:
            band[1] = y
            band[2] = min(band[2], min(cols))
            band[3] = max(band[3], max(cols))
    elif band is not None:
        print(f"rows {band[0]}..{band[1]}  ink x {band[2]}..{band[3]}")
        band = None

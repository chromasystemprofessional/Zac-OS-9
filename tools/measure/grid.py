"""Dump a region of a figure as a character grid (one char per pixel).

usage: grid.py NAME X Y W H
Grays map to their hex nibble (#000000 -> 0, #777777 -> 7, #ffffff -> f);
non-gray colors print as '*'.
"""
import sys
from PIL import Image

name, x0, y0, w, h = sys.argv[1], *map(int, sys.argv[2:6])
im = Image.open(f"figs/{name}.png").convert("RGB")
W, H = im.size
print(f"{name} {W}x{H} region x={x0}..{x0 + w - 1} y={y0}..{y0 + h - 1}")
print("     " + "".join(str((x0 + i) // 10 % 10) if (x0 + i) % 10 == 0 else " " for i in range(w)))
for y in range(y0, min(y0 + h, H)):
    row = []
    for x in range(x0, min(x0 + w, W)):
        r, g, b = im.getpixel((x, y))
        if r == g == b and r % 0x11 == 0:
            row.append("%x" % (r // 0x11))
        else:
            row.append("*")
    print(f"{y:4d} " + "".join(row))

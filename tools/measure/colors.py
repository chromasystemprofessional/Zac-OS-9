"""Print a region of a figure as a legend-coded color grid (for non-gray art).

usage: colors.py NAME X Y W H
Each distinct color gets a letter; the legend maps letters to hex values.
"""
import string
import sys

from PIL import Image

name, x0, y0, w, h = sys.argv[1], *map(int, sys.argv[2:6])
im = Image.open(f"figs/{name}.png").convert("RGB")
codes, legend = {}, []
letters = string.digits + string.ascii_letters
for y in range(y0, y0 + h):
    row = ""
    for x in range(x0, x0 + w):
        c = im.getpixel((x, y))
        if c not in codes:
            codes[c] = letters[len(codes)]
            legend.append(f"{codes[c]}=#{c[0]:02x}{c[1]:02x}{c[2]:02x}")
        row += codes[c]
    print(f"{y:4d} {row}")
print("legend:", " ".join(legend))

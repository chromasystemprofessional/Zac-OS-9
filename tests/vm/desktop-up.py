"""Exit 0 if a screenshot shows the ZacOS 9 menu bar, else 1.

usage: desktop-up.py SHOT.png

The menu bar is 20 px tall: a white top line, platinum gray with the
menu titles in black, a gray (153) shadow line at y=18 and a black line
across the whole screen at y=19.
"""
import sys

from PIL import Image

im = Image.open(sys.argv[1]).convert("RGB")
px = im.load()
w = im.width


def share(y, colour):
    return sum(px[x, y] == colour for x in range(w)) / w


titles = sum(px[x, y] == (0, 0, 0) for y in range(5, 14) for x in range(w))
ok = (share(0, (255, 255, 255)) > 0.9 and share(18, (153, 153, 153)) > 0.9 and
      share(19, (0, 0, 0)) == 1.0 and titles > 100)
sys.exit(0 if ok else 1)

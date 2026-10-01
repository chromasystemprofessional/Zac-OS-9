"""Print the screen position of a window's content, for UI tests.

usage: content-origin.py SHOT.png

Finds the topmost window frame below the menu bar: the first row with a
run of at least 120 black pixels (a frame's top line). Platinum
document frames put the content 22 px below that line and 6 px in from
the frame's left edge. Prints "X Y" of the content's top-left.
"""
import sys

from PIL import Image

im = Image.open(sys.argv[1]).convert("RGB")
px = im.load()
for y in range(21, im.height):
    run = 0
    for x in range(im.width):
        run = run + 1 if px[x, y] == (0, 0, 0) else 0
        if run >= 120:
            print(x - run + 1 + 6, y + 22)
            sys.exit(0)
sys.exit(1)

"""Print where the ZacOS9 arrow cursor is in screenshots (grim -c).

usage: find-cursor.py SHOT.png...

Matches the arrow's tip (assets/cursors/platinum-cursors.pcur, "default"):
its first three rows and its 13-pixel white left edge. Prints the
hotspot, one pixel right and down of the tip's corner.
"""
import sys

from PIL import Image

W, K = (255, 255, 255), (0, 0, 0)
TIP = ["WW..", "WKW.", "WKKW", "WKKKW"]

for path in sys.argv[1:]:
    im = Image.open(path).convert("RGB")
    px = im.load()
    found = None
    for y in range(im.height - 14):
        for x in range(im.width - 5):
            if px[x, y] != W or px[x + 1, y + 1] != K:
                continue
            ok = all(px[x + i, y + j] == (W if ch == "W" else K)
                     for j, row in enumerate(TIP) for i, ch in enumerate(row) if ch != ".")
            if ok and all(px[x, y + k] == W for k in range(13)):
                found = (x + 1, y + 1)
                break
        if found:
            break
    print(path, "cursor at", found)

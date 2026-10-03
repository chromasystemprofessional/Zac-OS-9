"""Sum up a recorded boot (tests/vm/record.py) as a timeline of what the
screen showed: white, the logo on white, black, text, the Welcome screen,
the desktop, or something else.

usage: boot-frames.py DIR

Prints runs of frames, e.g. "3.2 - 7.8 s  logo (24 frames)", and copies
the first frame of each run to DIR/run-NN-KIND.png for a look.
"""
import os
import shutil
import sys

from PIL import Image

d = sys.argv[1]
times = dict(line.split() for line in open(os.path.join(d, "times.txt")))
frames = sorted(f for f in os.listdir(d) if f.startswith("frame-"))
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)


def kind(path):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    small = im.resize((w // 4, h // 4), Image.NEAREST)
    px = list(small.getdata())
    n = len(px)
    white = sum(p == (255, 255, 255) for p in px)
    black = sum(p == (0, 0, 0) for p in px)
    if white == n:
        return "white"
    if white > 0.97 * n:
        # Everything that isn't white within a small box in the middle: the logo.
        sw, sh = small.size
        off = [(i % sw, i // sw) for i, p in enumerate(px) if p != (255, 255, 255)]
        xs, ys = [x for x, _ in off], [y for _, y in off]
        if max(xs) - min(xs) < sw // 8 and max(ys) - min(ys) < sh // 8 and \
                abs((max(xs) + min(xs)) / 2 - sw / 2) < 4 and abs((max(ys) + min(ys)) / 2 - sh / 2) < 4:
            return "logo"
        return "text-on-white"
    if black == n:
        return "black"
    if black > 0.85 * n:
        return "text-on-black"
    full = im.load()
    menubar = all(full[x, 19] == (0, 0, 0) for x in range(0, w, 7))
    if menubar:
        return "desktop"
    # The Welcome box (picture in the middle, so look just inside its left
    # edge): platinum grey, and the desktop pattern just outside it.
    if full[w // 2 - 150, h // 2] == (221, 221, 221) and full[w // 2 - 170, h // 2] != (221, 221, 221):
        return "welcome"
    return "other"


runs = []
for f in frames:
    k = kind(os.path.join(d, f))
    n = f[6:10].lstrip("0") or "0"
    t = float(times.get(n, 0))
    if runs and runs[-1][0] == k:
        runs[-1][2] = t
        runs[-1][3] += 1
    else:
        runs.append([k, t, t, 1, f])
for i, (k, t0, t1, count, first) in enumerate(runs):
    print(f"{t0:6.1f} - {t1:5.1f} s  {k} ({count} frames)")
    shutil.copy(os.path.join(d, first), os.path.join(d, f"run-{i:02d}-{k}.png"))

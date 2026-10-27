#!/usr/bin/env python3
"""ZacOS's Qt programs at 1.25x, 1.5x and 1.75x (wp_fractional_scale_v1,
now offered to them too; shell/common/pixels.h draws 1x images unsmoothed):
a control panel and its frame are exactly the 1x picture with each pixel
made one or two whole screen pixels (its text too, drawn as enlarged 1x
letters: ZACOS9_SCALED_FONTS=0; with the fonts for those scales its text
is drawn again in them, still in the 1x picture's colours) (lib/draw.c
pl_snap), every screen pixel compared but the pointer's, which zacos9-wm
draws from the cursor theme. Drawn at 2x and shrunk, as before, it was
blurred.

usage: test_panel_scale.py ZACOS9-WM CONTROLPANEL RAWSHOT
"""
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from zacos9test import wait_for

compositor, panel, rawshot = (str(Path(a).resolve()) for a in sys.argv[1:4])
POINTER = (98, 98)  # where zacos9-wm puts it, logical, with a pixel to spare


def snap(u, q):
    return (u * q + 2) // 4


def unsnap(d, q):
    return -((-(4 * d + 2)) // q) - 1


def session(scale, fonts=False):
    """(width, height, pixels) of the screen with the Mouse panel open, and
    the screen's pixels before it opened."""
    with tempfile.TemporaryDirectory(prefix="zacos9-panelscale-") as directory:
        root = Path(directory)
        (root / "zacos9").mkdir()
        (root / "zacos9" / "desktop.conf").write_text(f"scale={scale}\n")
        env = dict(os.environ, HOME=directory, XDG_RUNTIME_DIR=directory, XDG_CONFIG_HOME=directory,
                   XDG_DATA_HOME=directory, XDG_CACHE_HOME=directory,
                   WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
                   ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="", ZACOS9_COLLAR="",
                   ZACOS9_INHIBIT="", QT_QPA_PLATFORM="wayland", QT_STYLE_OVERRIDE="",
                   QT_WAYLAND_DISABLE_WINDOWDECORATION="1", ZACOS9_SCALED_FONTS="1" if fonts else "0")
        for key in ("WAYLAND_DISPLAY", "ZACOS9_SCALE", "QT_SCALE_FACTOR", "DISPLAY"):
            env.pop(key, None)
        processes = []
        with (root / "compositor.log").open("w+") as log:
            try:
                server = subprocess.Popen([compositor], env=env, stdout=log, stderr=log)
                processes.append(server)
                sockets = wait_for("the compositor", lambda: server.poll() is None and [
                    p for p in root.glob("wayland-*") if not p.name.endswith(".lock")])
                env["WAYLAND_DISPLAY"] = sockets[0].name
                path = root / "shot.ppm"

                def shot():
                    subprocess.run([rawshot, "HEADLESS-1", str(path)], env=env, check=True, timeout=10)
                    data = path.read_bytes()
                    header = data.split(b"\n", 3)
                    w, h = map(int, header[1].split())
                    return w, h, header[3]

                empty = shot()[2]
                processes.append(subprocess.Popen([panel, "mouse"], env=env, stdout=log, stderr=log))
                # Up, and drawn: the picture stops changing.
                last = [empty]

                def settled():
                    now = shot()
                    same = now[2] == last[0] and now[2] != empty
                    last[0] = now[2]
                    return same and now

                time.sleep(1.5)
                return wait_for("the panel", settled), empty
            except Exception:
                log.seek(0)
                print(log.read()[-3000:], file=sys.stderr)
                raise
            finally:
                for process in reversed(processes):
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()


def at(px, w, x, y):
    return px[(y * w + x) * 3:(y * w + x) * 3 + 3]


def window(w, h, px, empty, step=1):
    """The window's box (frame and shadow): where the screen changed."""
    xs, ys = [], []
    for y in range(0, h, step):
        for x in range(0, w, step):
            if at(px, w, x, y) != at(empty, w, x, y):
                xs.append(x)
                ys.append(y)
    return min(xs), min(ys), max(xs), max(ys)


failures = []
(w1, h1, one), empty1 = session(1)
box1 = window(w1, h1, one, empty1)
for scale in (1.25, 1.5, 1.75):
    q = round(scale * 4)
    (w, h, px), empty = session(scale)
    box = window(w, h, px, empty)
    # Where it is at this scale, logical: zacos9-wm may have moved it a
    # pixel or two onto where the screen's pixel pattern starts.
    ox, oy = unsnap(box[0], q) - box1[0], unsnap(box[1], q) - box1[1]
    compared = wrong = 0
    first = None

    def pointer(x, y):
        return POINTER[0] <= x < POINTER[0] + 22 and POINTER[1] <= y < POINTER[1] + 27

    for dy in range(box[1], box[3] + 1):
        v = unsnap(dy, q) - oy
        for dx in range(box[0], box[2] + 1):
            u = unsnap(dx, q) - ox
            # The pointer, where it is in either picture.
            if pointer(u + ox, v + oy) or pointer(u, v):
                continue
            if not (box1[0] <= u <= box1[2] and box1[1] <= v <= box1[3]):
                continue
            if 4 <= v - box1[1] < 16:
                continue  # the title bar's stripes: drawn for the scale (test-frame-scale)
            compared += 1
            a, b = at(px, w, dx, dy), at(one, w1, u, v)
            if a != b:
                wrong += 1
                first = first or (u - box1[0], v - box1[1], a.hex(), b.hex())
    ok = wrong == 0 and compared > 50000
    print(("ok: " if ok else "FAIL: ") + f"at {scale}x the Mouse panel is the 1x one, pixel for pixel "
          f"(moved {ox}, {oy}; {compared} compared, {wrong} wrong"
          f"{', first at ' + str(first) if first else ''})")
    if not ok:
        failures.append(scale)
    # With the font for this scale (the default): the text drawn again in it.
    (w2, h2, px2), _ = session(scale, fonts=True)
    colours1 = {at(one, w1, x, y) for y in range(box1[1], box1[3] + 1) for x in range(box1[0], box1[2] + 1)}
    redrawn = new = 0
    for dy in range(box[1], box[3] + 1):
        for dx in range(box[0], box[2] + 1):
            u, v = unsnap(dx, q), unsnap(dy, q)
            if pointer(u, v) or pointer(u - ox, v - oy):
                continue
            redrawn += at(px2, w2, dx, dy) != at(px, w, dx, dy)
            new += at(px2, w2, dx, dy) not in colours1
    ok = redrawn > 500 and new == 0
    print(("ok: " if ok else "FAIL: ") + f"at {scale}x its text in the font for {scale}x: "
          f"{redrawn} pixels redrawn, {new} in colours the 1x panel hasn't")
    if not ok:
        failures.append(scale)
sys.exit(1 if failures else 0)

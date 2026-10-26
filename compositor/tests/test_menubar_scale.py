#!/usr/bin/env python3
"""The menu bar at 1.25x, 1.5x and 1.75x (shell/menubar/main.c,
lib/draw.c pl_snap): drawn at the screen's own pixels through
wp_fractional_scale_v1, the bar and an open menu are exactly the 1x ones
with each pixel made one or two whole screen pixels (no blur from 2x
shrunk, no stray pixel): every screen pixel compared, with the text drawn
as enlarged 1x letters too (ZACOS9_SCALED_FONTS=0). With the fonts for
those scales (the default), the text is drawn again in them: different
pixels, but still only the 1x picture's colours (crisp).

usage: test_menubar_scale.py ZACOS9-WM MENUBAR VPTR RAWSHOT
"""
import os
import shlex
import subprocess
import sys
import tempfile
import time
from pathlib import Path

compositor, menubar, vptr, rawshot = (str(Path(a).resolve()) for a in sys.argv[1:5])
AREA = 400  # logical pixels from the top left: the logo menu and the bar's left


def snap(u, q):
    return (u * q + 2) // 4


def unsnap(d, q):
    return -((-(4 * d + 2)) // q) - 1


def wait_for(what, check, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(0.1)
    raise AssertionError(f"timed out waiting for {what}")


def session(scale, fonts=True):
    """(before, after opening the logo menu): {(x, y): rgb} readers, and the width."""
    with tempfile.TemporaryDirectory(prefix="zacos9-barscale-") as directory:
        root = Path(directory)
        for d in ("config/zacos9", "data", "cache", "home"):
            (root / d).mkdir(parents=True)
        (root / "config/zacos9/desktop.conf").write_text(f"scale={scale}\nclock=off\nhint-bubbles=0\n")
        env = dict(os.environ, XDG_RUNTIME_DIR=directory, HOME=str(root / "home"),
                   XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
                   XDG_CACHE_HOME=str(root / "cache"),
                   WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
                   ZACOS9_STARTUP="0", ZACOS9_COLLAR="", ZACOS9_INHIBIT="", ZACOS9_FINDER="",
                   ZACOS9_MENUBAR=shlex.quote(menubar), ZACOS9_SCALED_FONTS="1" if fonts else "0")
        env.pop("WAYLAND_DISPLAY", None)
        env.pop("ZACOS9_SCALE", None)
        with (root / "compositor.log").open("w+") as log:
            server = subprocess.Popen([compositor], env=env, stdout=log, stderr=log)
            try:
                sockets = wait_for("the compositor", lambda: server.poll() is None and [
                    p for p in root.glob("wayland-*") if not p.name.endswith(".lock")])
                env["WAYLAND_DISPLAY"] = sockets[0].name

                def shot():
                    path = root / "shot.ppm"
                    subprocess.run([rawshot, "HEADLESS-1", str(path)], env=env, check=True, timeout=10)
                    data = path.read_bytes()
                    fields, at = [], 2
                    while len(fields) < 3:
                        while data[at:at + 1].isspace():
                            at += 1
                        end = at
                        while not data[end:end + 1].isspace():
                            end += 1
                        fields.append(int(data[at:end]))
                        at = end
                    w, px = fields[0], data[at + 1:]
                    return lambda x, y: px[(y * w + x) * 3:(y * w + x) * 3 + 3]

                def vp(*args):
                    subprocess.run([vptr, *args], env=env, check=True, timeout=20)

                # The pointer out of the way: a software cursor is in the picture.
                vp("home", "move", "700", "600")
                q = round(scale * 4)
                wait_for("the menu bar", lambda: shot()(snap(10, q), snap(10, q)) == bytes((0xDD,) * 3))
                time.sleep(0.5)
                before = shot()
                vp("home", "move", "20", "10", "click", "move", "680", "590", "wait", "500")
                return before, shot()
            except Exception:
                log.seek(0)
                print(log.read()[-3000:], file=sys.stderr)
                raise
            finally:
                server.terminate()
                try:
                    server.wait(5)
                except subprocess.TimeoutExpired:
                    server.kill()


failures = []
before1, after1 = session(1)
menu = {(x, y) for y in range(20, AREA) for x in range(AREA) if before1(x, y) != after1(x, y)}
if len(menu) < 2000:
    print(f"FAIL: the logo menu didn't open at 1x ({len(menu)} pixels changed)")
    sys.exit(1)
for scale in (1.25, 1.5, 1.75):
    q = round(scale * 4)
    _, after = session(scale, fonts=False)
    compared = wrong = 0
    first = None
    for dy in range(snap(AREA, q)):
        v = unsnap(dy, q)
        for dx in range(snap(AREA, q)):
            u = unsnap(dx, q)
            if v < 20 or (u, v) in menu:
                compared += 1
                if after(dx, dy) != after1(u, v):
                    wrong += 1
                    first = first or (dx, dy, after(dx, dy).hex(), after1(u, v).hex())
    ok = wrong == 0 and compared > 3000
    print(("ok: " if ok else "FAIL: ") + f"at {scale}x the bar and the open menu are the 1x ones, "
          f"pixel for pixel ({compared} compared, {wrong} wrong{', first ' + str(first) if first else ''})")
    if not ok:
        failures.append(scale)
    _, fonts = session(scale)
    colours1 = {after1(u, v) for v in range(AREA) for u in range(AREA)}
    redrawn = new = 0
    for dy in range(snap(AREA, q)):
        for dx in range(snap(AREA, q)):
            redrawn += fonts(dx, dy) != after(dx, dy)
            new += fonts(dx, dy) not in colours1
    ok = redrawn > 200 and new == 0
    print(("ok: " if ok else "FAIL: ") + f"at {scale}x its text in the font for {scale}x: "
          f"{redrawn} pixels redrawn, {new} in colours the 1x picture hasn't")
    if not ok:
        failures.append(scale)
sys.exit(1 if failures else 0)

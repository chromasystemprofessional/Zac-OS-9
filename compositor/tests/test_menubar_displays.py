#!/usr/bin/env python3
"""The menu bar on several screens (shell/menubar/main.c, compositor/src/
layers.c): two headless screens side by side, the second at 2x
("display.HEADLESS-2.scale=2"):

- without "menubar-every-display", the bar is on the main screen only;
- with it (the Monitors panel's Menu Bar on Every Display, applied at
  once), each screen has one, drawn at that screen's scale: the 2x one's
  1-pixel lines are two pixels thick;
- a menu opens on the screen whose bar was clicked, not on the main one;
- turned off again, the second screen's bar goes.

usage: test_menubar_displays.py ZACOS9-WM MENUBAR VPTR
"""
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

compositor, menubar, vptr = (str(Path(a).resolve()) for a in sys.argv[1:4])
if not shutil.which("grim"):
    print("SKIP: grim is not installed")
    sys.exit(77)
GRAY_DDD, GRAY_999, BLACK = (0xDD,) * 3, (0x99,) * 3, (0, 0, 0)
BASE = ("main-display=HEADLESS-1\ndisplay.HEADLESS-1.x=0\ndisplay.HEADLESS-1.y=0\n"
        "display.HEADLESS-2.x=1280\ndisplay.HEADLESS-2.y=0\ndisplay.HEADLESS-2.scale=2\n")


def wait_for(what, check, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(0.1)
    raise AssertionError(f"timed out waiting for {what}")


failures = []


def check(ok, what):
    print(("ok: " if ok else "FAIL: ") + what)
    if not ok:
        failures.append(what)


with tempfile.TemporaryDirectory(prefix="zacos9-bars-") as directory:
    root = Path(directory)
    for d in ("config/zacos9", "data", "cache", "home"):
        (root / d).mkdir(parents=True)
    conf = root / "config/zacos9/desktop.conf"
    conf.write_text(BASE)
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, HOME=str(root / "home"),
               XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
               XDG_CACHE_HOME=str(root / "cache"),
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="2",
               ZACOS9_STARTUP="0", ZACOS9_COLLAR="", ZACOS9_INHIBIT="", ZACOS9_FINDER="",
               ZACOS9_MENUBAR=shlex.quote(menubar))
    env.pop("WAYLAND_DISPLAY", None)
    env.pop("ZACOS9_SCALE", None)
    with (root / "compositor.log").open("w+") as log:
        server = subprocess.Popen([compositor], env=env, stdout=log, stderr=log)
        try:
            sockets = wait_for("the compositor", lambda: server.poll() is None and [
                p for p in root.glob("wayland-*") if not p.name.endswith(".lock")])
            env["WAYLAND_DISPLAY"] = sockets[0].name
            outputs = root / f"zacos9-outputs-{sockets[0].name}"

            def placed():
                text = outputs.read_text() if outputs.exists() else ""
                return " HEADLESS-2 1280x720 scale 2 " in text.replace("output", "") and \
                    "x 1280 y 0" in text

            wait_for("the screens placed", placed)

            def shot(output):
                """{(x, y): (r, g, b)} reader for one screen, in its pixels."""
                path = root / f"{output}.ppm"
                subprocess.run(["grim", "-t", "ppm", "-o", output, str(path)], env=env,
                               check=True, timeout=10)
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
                width, pixels = fields[0], data[at + 1:]
                return lambda x, y: tuple(pixels[(y * width + x) * 3:(y * width + x) * 3 + 3])

            def has_bar(output, scale, x):
                """The bar's grays at logical column x: face, then 999, then black."""
                px = shot(output)
                return (px(x * scale, 10 * scale) == GRAY_DDD and
                        all(px(x * scale, 18 * scale + i) == GRAY_999 for i in range(scale)) and
                        all(px(x * scale, 19 * scale + i) == BLACK for i in range(scale)))

            def vp(*args):
                subprocess.run([vptr, *args], env=env, check=True, timeout=20)

            wait_for("the bar on the main screen", lambda: has_bar("HEADLESS-1", 1, 600))
            time.sleep(0.5)
            check(not has_bar("HEADLESS-2", 2, 320), "one bar, on the main screen only")

            conf.write_text(BASE + "menubar-every-display=1\n")
            wait_for("a bar on the second screen", lambda: has_bar("HEADLESS-2", 2, 320))
            check(has_bar("HEADLESS-1", 1, 600), "with Menu Bar on Every Display, both screens have one")
            check(has_bar("HEADLESS-2", 2, 320), "the 2x screen's drawn at 2x: its lines two pixels thick")

            # The logo menu from the second screen's bar.
            below = lambda output, s: [shot(output)(x * s, 60 * s) for x in range(4, 120, 8)]
            before1, before2 = below("HEADLESS-1", 1), below("HEADLESS-2", 2)
            vp("home", "move", "1300", "10", "click")
            wait_for("a menu on the second screen", lambda: below("HEADLESS-2", 2) != before2)
            check(below("HEADLESS-1", 1) == before1, "the menu opens on the screen clicked, not the main one")
            vp("key", "Escape")
            wait_for("the menu closed", lambda: below("HEADLESS-2", 2) == before2)

            conf.write_text(BASE + "menubar-every-display=0\n")
            wait_for("the second bar gone", lambda: not has_bar("HEADLESS-2", 2, 320))
            check(has_bar("HEADLESS-1", 1, 600), "turned off, the main screen keeps its bar")
        except Exception:
            log.seek(0)
            print(log.read()[-4000:], file=sys.stderr)
            raise
        finally:
            server.terminate()
            try:
                server.wait(5)
            except subprocess.TimeoutExpired:
                server.kill()

sys.exit(1 if failures else 0)

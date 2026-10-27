#!/usr/bin/env python3
"""Screens start at the origin (compositor/src/prefs.c, apply_layout): a
screen the Monitors panel placed that isn't connected leaves no empty strip
before the others. With one left (DVI-D-1 at x 0, HDMI at 1280, DVI gone),
X11's root was wider than its screen and Krita's tablet pen landed in the
wrong place.

- one screen, saved at x 1280 beside a missing one: at 0, 0;
- two screens saved right of a missing one: both moved left together,
  side by side as saved;
- saved below and right (y too): moved up as well.

usage: test_display_origin.py ZACOS9-WM
"""
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

compositor = str(Path(sys.argv[1]).resolve())
failures = 0


def check(ok, what):
    global failures
    print(("ok: " if ok else "FAIL: ") + what)
    failures += not ok


def placements(conf, screens):
    """The compositor's output status (name -> (x, y)) for this desktop.conf."""
    with tempfile.TemporaryDirectory(prefix="zacos9-origin-") as directory:
        root = Path(directory)
        (root / "config/zacos9").mkdir(parents=True)
        (root / "config/zacos9/desktop.conf").write_text(conf)
        env = dict(os.environ, XDG_RUNTIME_DIR=directory, HOME=directory,
                   XDG_CONFIG_HOME=str(root / "config"),
                   WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS=str(screens),
                   ZACOS9_STARTUP="0", ZACOS9_COLLAR="", ZACOS9_INHIBIT="", ZACOS9_FINDER="",
                   ZACOS9_MENUBAR="")
        env.pop("WAYLAND_DISPLAY", None)
        env.pop("ZACOS9_SCALE", None)
        server = subprocess.Popen([compositor], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                for status in root.glob("zacos9-outputs-*"):
                    found = {}
                    for line in status.read_text().splitlines():
                        words = line.split()
                        if words[:1] == ["output"]:
                            found[words[1]] = (int(words[words.index("x") + 1]), int(words[words.index("y") + 1]))
                    if len(found) == screens:
                        return found
                time.sleep(0.05)
            raise AssertionError("the compositor never wrote where its screens are")
        finally:
            server.terminate()
            server.wait(timeout=5)


one = placements("main-display=HEADLESS-1\ndisplay.DVI-D-1.x=0\ndisplay.DVI-D-1.y=0\n"
                 "display.HEADLESS-1.x=1280\ndisplay.HEADLESS-1.y=0\n", 1)
check(one == {"HEADLESS-1": (0, 0)}, f"one screen beside a missing one starts at the origin {one}")

two = placements("display.DVI-D-1.x=0\ndisplay.DVI-D-1.y=0\n"
                 "display.HEADLESS-1.x=1280\ndisplay.HEADLESS-1.y=0\n"
                 "display.HEADLESS-2.x=2560\ndisplay.HEADLESS-2.y=0\n", 2)
check(two == {"HEADLESS-1": (0, 0), "HEADLESS-2": (1280, 0)},
      f"two screens move left together, side by side as saved {two}")

below = placements("display.HEADLESS-1.x=300\ndisplay.HEADLESS-1.y=200\n", 1)
check(below == {"HEADLESS-1": (0, 0)}, f"saved lower down, it moves up too {below}")
sys.exit(1 if failures else 0)

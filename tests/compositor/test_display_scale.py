#!/usr/bin/env python3
"""The Monitors panel's scale belongs to the screen it was chosen on
(compositor/src/prefs.c, display_setting): "scale" alone is that screen's
when it is the only one, and with several it is the main screen's, the
others keeping their own (picked from their density).

- two screens, "scale=2": the main one at 2x, the other at 1x;
- the other screen's own display.NAME.scale is used instead;
- one screen, "scale=2": 2x, as before.

usage: test_display_scale.py ZACOS9-WM
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


def screens(conf, count):
    """name -> (scale, main) from the compositor's output status."""
    with tempfile.TemporaryDirectory(prefix="zacos9-scale-") as directory:
        root = Path(directory)
        (root / "config/zacos9").mkdir(parents=True)
        (root / "config/zacos9/desktop.conf").write_text(conf)
        env = dict(os.environ, XDG_RUNTIME_DIR=directory, HOME=directory,
                   XDG_CONFIG_HOME=str(root / "config"),
                   WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS=str(count),
                   ZACOS9_STARTUP="0", ZACOS9_COLLAR="", ZACOS9_INHIBIT="", ZACOS9_FINDER="",
                   ZACOS9_MENUBAR="")
        env.pop("WAYLAND_DISPLAY", None)
        env.pop("ZACOS9_SCALE", None)
        server = subprocess.Popen([compositor], env=env, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                for status in root.glob("zacos9-outputs-*"):
                    found = {}
                    for line in status.read_text().splitlines():
                        words = line.split()
                        if words[:1] == ["output"]:
                            found[words[1]] = (float(words[words.index("scale") + 1]),
                                               int(words[words.index("main") + 1]))
                    if len(found) == count and sum(main for _, main in found.values()) == 1:
                        return found
                time.sleep(0.05)
            raise AssertionError("the compositor never wrote its screens' scales")
        finally:
            server.terminate()
            server.wait(timeout=5)


two = screens("main-display=HEADLESS-1\nscale=2\n", 2)
check(two == {"HEADLESS-1": (2.0, 1), "HEADLESS-2": (1.0, 0)},
      f"the main screen's scale, and only it {two}")

own = screens("main-display=HEADLESS-1\nscale=2\ndisplay.HEADLESS-2.scale=2\n", 2)
check(own == {"HEADLESS-1": (2.0, 1), "HEADLESS-2": (2.0, 0)},
      f"a screen's own scale still applies {own}")

one = screens("scale=2\n", 1)
check(one == {"HEADLESS-1": (2.0, 1)}, f"one screen keeps the panel's scale {one}")

sys.exit(1 if failures else 0)

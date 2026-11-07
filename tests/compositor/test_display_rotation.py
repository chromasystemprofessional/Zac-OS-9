#!/usr/bin/env python3
"""The Monitors panel's rotation (compositor/src/prefs.c).

"rotation" is the main (or only) screen's; "display.NAME.rotation" is one
screen's own. The transform is published back, and the layout uses the
rotated logical size: two 1024x768 screens, the first at 90 degrees, put
the second at x=768.

usage: test_display_rotation.py ZACOS9-WM
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


def screens(conf, count, system=""):
    """name -> (rotation, x, width, height) from the output status."""
    with tempfile.TemporaryDirectory(prefix="zacos9-rotate-") as directory:
        root = Path(directory)
        (root / "config/zacos9").mkdir(parents=True)
        (root / "config/zacos9/desktop.conf").write_text(conf)
        (root / "system.conf").write_text(system)
        env = dict(os.environ, XDG_RUNTIME_DIR=directory, HOME=directory,
                   XDG_CONFIG_HOME=str(root / "config"),
                   ZACOS9_SYSTEM_CONF=str(root / "system.conf"),
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
                        if words[:1] != ["output"]:
                            continue
                        wh = words[2].split("x")
                        found[words[1]] = (
                            int(words[words.index("rotate") + 1]),
                            int(words[words.index("x") + 1]),
                            int(wh[0]),
                            int(wh[1]),
                        )
                    if len(found) == count:
                        return found
                time.sleep(0.05)
            raise AssertionError("the compositor never wrote its screens' rotation")
        finally:
            server.terminate()
            server.wait(timeout=5)


default = screens("", 2)
check(all(r == 0 for r, _, _, _ in default.values()), f"upright by default {default}")
check(sorted(x for _, x, _, _ in default.values()) == [0, 1280],
      f"an upright screen keeps its 1280 px width in the layout {default}")

one = screens("rotation=90\n", 1)
check(one["HEADLESS-1"][0] == 90, f"one screen can be turned 90 degrees {one}")

two = screens("main-display=HEADLESS-1\nrotation=90\n", 2)
check(two["HEADLESS-1"][0] == 90, f"the main screen's rotation applies {two}")

layout = screens("display.HEADLESS-2.rotation=90\n", 2)
check(layout["HEADLESS-2"][0] == 90, f"a screen's own rotation applies {layout}")
check(sorted(x for _, x, _, _ in layout.values()) == [0, 720],
      f"the layout uses the rotated 720 px width {layout}")

own = screens("main-display=HEADLESS-1\nrotation=90\ndisplay.HEADLESS-2.rotation=270\n", 2)
check(own["HEADLESS-1"][0] == 90 and own["HEADLESS-2"][0] == 270,
      f"both screens keep their own rotation {own}")

# The login screen has no account of its own: /etc/zacos9/desktop.conf is
# the machine-wide fallback, and an account's own setting still wins.
machine = screens("", 1, system="rotation=90\n")
check(machine["HEADLESS-1"][0] == 90, f"the machine-wide rotation applies {machine}")
machine_display = screens("", 2, system="display.HEADLESS-2.rotation=180\n")
check(machine_display["HEADLESS-2"][0] == 180,
      f"a machine-wide screen rotation applies {machine_display}")
account = screens("rotation=270\n", 1, system="rotation=90\n")
check(account["HEADLESS-1"][0] == 270,
      f"the account's rotation wins over the machine-wide one {account}")

sys.exit(1 if failures else 0)

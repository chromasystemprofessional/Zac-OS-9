#!/usr/bin/env python3
"""The Monitors panel's 1.25x, 1.5x and 1.75x, for every screen or one
(compositor/src/prefs.c, xwayland_output.c): zacos9-wm takes "scale=1.5"
(or "display.NAME.scale=2" for one screen) from desktop.conf and
publishes it in zacos9-outputs; programs are offered
wp_fractional_scale_v1 to draw at exactly 1.5, and 2x buffers otherwise
(wl_output.scale 2); ZacOS's own programs (named zacos9-…) too, as they
draw their pixel art at the screen's own pixels (test-menubar-scale,
test-panel-scale). Whole-number scales are unchanged.

usage: test_fractional_scale.py ZACOS9-WM GLOBALS-CLIENT
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from zacos9test import wait_for

compositor, probe = (str(Path(a).resolve()) for a in sys.argv[1:3])
FRACTIONAL = "wp_fractional_scale_manager_v1"


def session(scale, conf="", screens=1):
    """(the first outputs line, a program's globals, a zacos9- program's
    globals), or with several screens every outputs line for the first."""
    with tempfile.TemporaryDirectory(prefix="zacos9-fractional-") as directory:
        root = Path(directory)
        (root / "zacos9").mkdir()
        (root / "zacos9" / "desktop.conf").write_text(f"scale={scale}\n{conf}")
        env = dict(os.environ, HOME=directory, XDG_RUNTIME_DIR=directory,
                   XDG_CONFIG_HOME=directory, WLR_BACKENDS="headless", WLR_RENDERER="pixman",
                   WLR_HEADLESS_OUTPUTS=str(screens), ZACOS9_STARTUP="0", ZACOS9_MENUBAR="",
                   ZACOS9_FINDER="", ZACOS9_COLLAR="", ZACOS9_INHIBIT="")
        env.pop("WAYLAND_DISPLAY", None)
        env.pop("ZACOS9_SCALE", None)
        ours = root / "zacos9-probe"
        shutil.copy(probe, ours)
        with (root / "compositor.log").open("w+") as log:
            server = subprocess.Popen([compositor], env=env, stdout=log, stderr=log)
            try:
                outputs = wait_for("zacos9-outputs", lambda: len(lines := [
                    line for p in root.glob("zacos9-outputs-*")
                    for line in p.read_text().splitlines() if line.startswith("output ")]) >= screens
                    and lines)
                status = next(root.glob("zacos9-outputs-*")).name
                env["WAYLAND_DISPLAY"] = status[len("zacos9-outputs-"):]
                other = subprocess.run([probe], env=env, check=True, capture_output=True,
                                       text=True, timeout=10).stdout.splitlines()
                own = subprocess.run([str(ours)], env=env, check=True, capture_output=True,
                                     text=True, timeout=10).stdout.splitlines()
                return (outputs[0] if screens == 1 else outputs), other, own
            finally:
                server.terminate()
                server.wait(5)


failures = []


def check(ok, what):
    print(("ok: " if ok else "FAIL: ") + what)
    if not ok:
        failures.append(what)


line, other, own = session("1.5")
check(" scale 1.5 " in line, f"1.5 published in zacos9-outputs ({line})")
check(FRACTIONAL in other, "a program is offered wp_fractional_scale_v1")
check("output-scale 2" in other, "and 2x buffers if it draws at whole scales")
check(FRACTIONAL in own and "output-scale 2" in own,
      "a zacos9- program is offered it too (and 2x if it can't take it)")

line, other, _ = session("1.25")
check(" scale 1.25 " in line and "output-scale 2" in other, f"1.25 too ({line})")

line, other, _ = session("1.75")
check(" scale 1.75 " in line and "output-scale 2" in other, f"1.75 too ({line})")

# Each screen its own: the second at 2x, the first at the general 1x.
lines, other, _ = session("1", "display.HEADLESS-2.scale=2\n", screens=2)
scales = {l.split()[1]: l.split()[4] for l in lines}
check(scales == {"HEADLESS-1": "1", "HEADLESS-2": "2"}, f"one screen at 2x, the other at 1x ({lines})")
check("output-scale 1" in other and "output-scale 2" in other,
      "programs see each screen's own scale")
lines, _, _ = session("1.5", "display.HEADLESS-1.scale=1\n", screens=2)
scales = {l.split()[1]: l.split()[4] for l in lines}
check(scales == {"HEADLESS-1": "1", "HEADLESS-2": "1.5"}, f"a screen of its own overrides the general scale ({lines})")

line, other, _ = session("2")
check(" scale 2 " in line and "output-scale 2" in other, f"2 as before ({line})")

line, other, _ = session("1.7")
check(" scale 1 " in line, f"a scale the panel doesn't offer is ignored ({line})")

sys.exit(1 if failures else 0)

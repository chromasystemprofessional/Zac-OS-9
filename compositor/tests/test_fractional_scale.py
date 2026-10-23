#!/usr/bin/env python3
"""The Monitors panel's 1.25x and 1.5x (compositor/src/prefs.c,
xwayland_output.c): zacos9-wm takes "scale=1.5" from desktop.conf and
publishes it in zacos9-outputs; programs are offered
wp_fractional_scale_v1 to draw at exactly 1.5, and 2x buffers otherwise
(wl_output.scale 2); ZacOS's own programs (named zacos9-…) are not offered
it, so their pixel art is drawn at 2x. Whole-number scales are unchanged.

usage: test_fractional_scale.py ZACOS9-WM GLOBALS-CLIENT
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

compositor, probe = (str(Path(a).resolve()) for a in sys.argv[1:3])
FRACTIONAL = "wp_fractional_scale_manager_v1"


def wait_for(what, check, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError(f"timed out waiting for {what}")


def session(scale):
    """(the outputs line, a program's globals, a zacos9- program's globals)."""
    with tempfile.TemporaryDirectory(prefix="zacos9-fractional-") as directory:
        root = Path(directory)
        (root / "zacos9").mkdir()
        (root / "zacos9" / "desktop.conf").write_text(f"scale={scale}\n")
        env = dict(os.environ, HOME=directory, XDG_RUNTIME_DIR=directory,
                   XDG_CONFIG_HOME=directory, WLR_BACKENDS="headless", WLR_RENDERER="pixman",
                   WLR_HEADLESS_OUTPUTS="1", ZACOS9_STARTUP="0", ZACOS9_MENUBAR="",
                   ZACOS9_FINDER="", ZACOS9_COLLAR="", ZACOS9_INHIBIT="")
        env.pop("WAYLAND_DISPLAY", None)
        env.pop("ZACOS9_SCALE", None)
        ours = root / "zacos9-probe"
        shutil.copy(probe, ours)
        with (root / "compositor.log").open("w+") as log:
            server = subprocess.Popen([compositor], env=env, stdout=log, stderr=log)
            try:
                outputs = wait_for("zacos9-outputs", lambda: [
                    line for p in root.glob("zacos9-outputs-*")
                    for line in p.read_text().splitlines() if line.startswith("output ")])
                status = next(root.glob("zacos9-outputs-*")).name
                env["WAYLAND_DISPLAY"] = status[len("zacos9-outputs-"):]
                other = subprocess.run([probe], env=env, check=True, capture_output=True,
                                       text=True, timeout=10).stdout.splitlines()
                own = subprocess.run([str(ours)], env=env, check=True, capture_output=True,
                                     text=True, timeout=10).stdout.splitlines()
                return outputs[0], other, own
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
check(FRACTIONAL not in own and "wl_compositor" in own,
      "a zacos9- program is not offered it (draws at 2x)")
check("output-scale 2" in own, "a zacos9- program draws at 2x")

line, other, _ = session("1.25")
check(" scale 1.25 " in line and "output-scale 2" in other, f"1.25 too ({line})")

line, other, _ = session("2")
check(" scale 2 " in line and "output-scale 2" in other, f"2 as before ({line})")

line, other, _ = session("1.7")
check(" scale 1 " in line, f"a scale the panel doesn't offer is ignored ({line})")

sys.exit(1 if failures else 0)

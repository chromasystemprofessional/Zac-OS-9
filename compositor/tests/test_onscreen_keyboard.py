#!/usr/bin/env python3
"""The on-screen keyboard (compositor/src/osk.c, shell/keyboard/keyboard.c)
in a headless zacos9-wm, which has no keyboard attached, nor a touchscreen
(so "onscreen-keyboard=true", as with one):

- a Qt window's text field taking the focus brings up zacos9-keyboard
  along the bottom of the screen;
- clicking its keys types into the field, the field keeping the focus;
  Shift is down for the next key only;
- the window closing takes it away;
- in a window of ZacOS's own (test-field-client, which says where its
  focus is over platinum-shell), its Tab key moving the focus from the
  field to a button takes it away too. (Qt's text-input alone says
  nothing of that.)

usage: test_onscreen_keyboard.py ZACOS9-WM IDLE-CLIENT FIELD-CLIENT VPTR RAWSHOT
"""
import os
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

from zacos9test import wait_for

compositor, client, field_client, vptr, rawshot = (str(Path(a).resolve()) for a in sys.argv[1:6])
FACE = bytes((0xCC, 0xCC, 0xCC))

# keyboard.c's layout on a 1280 x 720 screen at 1x: keys 48 high, 84.8 wide
# a unit, from 6 in; the keyboard 270 high at the bottom.
TOP = 720 - (2 + 6 * 2 + 5 * 48 + 4 * 4)
UNIT = (1280 - 12 + 4) / 15


def key(row, before, width=1.0):
    """The middle of a key: its row, and the units of keys before it."""
    x = 6 + before * UNIT + (width * UNIT - 4) / 2
    y = TOP + 2 + 6 + row * (48 + 4) + 24
    return round(x), round(y)


H, I, SHIFT, TAB = key(2, 1.75 + 5), key(1, 1.5 + 7), key(3, 0, 2.25), key(1, 0, 1.5)

failures = []


def check(ok, what):
    print(("ok: " if ok else "FAIL: ") + what)
    if not ok:
        failures.append(what)


def running():
    return subprocess.run(["pgrep", "-x", "zacos9-keyboard"], capture_output=True).returncode == 0


with tempfile.TemporaryDirectory(prefix="zacos9-osk-") as directory:
    root = Path(directory)
    (root / "zacos9").mkdir()
    (root / "zacos9" / "desktop.conf").write_text("onscreen-keyboard=true\n")
    env = dict(os.environ, HOME=str(root), XDG_RUNTIME_DIR=str(root), XDG_CONFIG_HOME=str(root),
               XDG_DATA_HOME=str(root), XDG_CACHE_HOME=str(root), XDG_STATE_HOME=str(root),
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
               ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="", ZACOS9_COLLAR="",
               ZACOS9_INHIBIT="", QT_QPA_PLATFORM="wayland", QT_STYLE_OVERRIDE="",
               QT_WAYLAND_DISABLE_WINDOWDECORATION="1")
    for name in ("WAYLAND_DISPLAY", "ZACOS9_SCALE", "DISPLAY", "ZACOS9_KEYBOARD"):
        env.pop(name, None)
    log = (root / "log").open("w+")
    processes = [subprocess.Popen([compositor], env=env, stdout=log, stderr=log)]
    try:
        sockets = wait_for("a compositor", lambda: [p for p in root.glob("wayland-*")
                                                     if not p.name.endswith(".lock")])
        env["WAYLAND_DISPLAY"] = sockets[0].name
        state = root / f"zacos9-keyboard-{sockets[0].name}"

        def start(program):
            lines = []
            proc = subprocess.Popen([program, "--field"], env=env, stdout=subprocess.PIPE, stderr=log, text=True)
            processes.append(proc)
            threading.Thread(target=lambda: [lines.append(l.strip()) for l in proc.stdout],
                             daemon=True).start()
            wait_for("the window", lambda: "shown" in lines)
            return proc, lines

        def shown():
            return state.exists() and state.read_text().strip() == "shown 1" and running()

        def gone():
            return state.read_text().strip() == "shown 0" and not running()

        def attempt(what, cond, seconds=5):
            try:
                wait_for(what, cond, seconds)
                check(True, what)
            except AssertionError:
                check(False, what)

        proc, lines = start(client)
        attempt("a focused text field with no keyboard attached brings up the keyboard", shown, 10)

        def drawn():
            path = root / "shot.ppm"
            if subprocess.run([rawshot, "HEADLESS-1", str(path)], env=env,
                              stderr=subprocess.DEVNULL).returncode != 0:
                return False
            data = path.read_bytes().split(b"\n", 3)
            w = int(data[1].split()[0])
            px = data[3]
            at = lambda x, y: px[(y * w + x) * 3:(y * w + x) * 3 + 3]
            return all(at(x, TOP + 4) == FACE for x in (2, 640, 1277))
        attempt("the keyboard is along the bottom of the screen", drawn, 10)

        def click(*points, wait=300):
            for x, y in points:
                subprocess.run([vptr, "home", "move", str(x), str(y), "click", "wait", str(wait)], env=env,
                               check=True, timeout=20)

        click(H, I)
        attempt("its keys type into the field, which keeps the focus", lambda: "text hi" in lines)
        click(SHIFT, H, H)
        attempt("Shift is down for the next key only", lambda: "text hiHh" in lines)

        proc.terminate()
        proc.wait(timeout=5)
        attempt("the window with the field closing takes the keyboard away", gone)

        proc, lines = start(field_client)
        attempt("a field in one of ZacOS's own windows brings it up", shown, 10)
        click(TAB)
        attempt("there, Tab moving the focus to a button takes it away", gone)
    except Exception:
        log.seek(0)
        print(log.read()[-3000:], file=sys.stderr)
        raise
    finally:
        for p in reversed(processes):
            p.terminate()
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()
        subprocess.run(["pkill", "-x", "zacos9-keyboard"])

sys.exit(1 if failures else 0)

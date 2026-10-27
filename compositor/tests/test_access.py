"""Easy Access and Zoom (compositor/src/access.c): Sticky Keys latches and
locks modifiers, Shift five times turns it off; ⌘-Shift-Num Lock turns on
Mouse Keys, whose keypad moves the pointer and clicks; ⌘-Option-K zooms the
screen about the pointer.

usage: test_access.py ZACOS9-WM IDLE-CLIENT VPTR
"""
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

from zacos9test import wait_for

compositor, client, vptr = (str(Path(a).resolve()) for a in sys.argv[1:4])


with tempfile.TemporaryDirectory(prefix="zacos9-access-") as directory:
    root = Path(directory)
    (root / "zacos9").mkdir()
    conf = root / "zacos9" / "desktop.conf"
    conf.write_text("[General]\nsticky-keys=true\n")
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_CONFIG_HOME=directory,
               XDG_DATA_HOME=str(root / "data"),
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
               ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="", ZACOS9_COLLAR="",
               ZACOS9_INHIBIT="", QT_QPA_PLATFORM="wayland", QT_STYLE_OVERRIDE="",
               QT_WAYLAND_DISABLE_WINDOWDECORATION="1")
    env.pop("WAYLAND_DISPLAY", None)
    processes = []
    with (root / "compositor.log").open("w+") as log:
        try:
            server = subprocess.Popen([compositor], env=env, stdout=log, stderr=log)
            processes.append(server)
            sockets = wait_for("the compositor", lambda: server.poll() is None and [
                p for p in root.glob("wayland-*") if not p.name.endswith(".lock")])
            env["WAYLAND_DISPLAY"] = sockets[0].name

            def vp(*args):
                subprocess.run([vptr, *args], env=env, check=True, timeout=20)

            lines = []
            p = subprocess.Popen([client, "--keys", "--app-id", "alpha"], env=env,
                                 stdout=subprocess.PIPE, stderr=log, text=True)
            processes.append(p)
            threading.Thread(target=lambda: [lines.append(l.rstrip("\n")) for l in p.stdout],
                             daemon=True).start()
            wait_for("the window", lambda: "shown" in lines)

            def typed():
                return "".join(l[4:] for l in lines if l.startswith("key "))

            def settings():
                return conf.read_text()

            # ---- Sticky Keys ----
            vp("hold", "shift", "release", "shift", "wait", "100", "type", "a", "wait", "100",
               "type", "b", "wait", "300")
            wait_for("typed text", lambda: len(typed()) >= 2)
            assert typed() == "Ab", f"Shift pressed alone applies to the next key only: {typed()!r}"
            print("ok: Sticky Keys: a modifier pressed alone applies to the next key")

            vp("hold", "shift", "release", "shift", "hold", "shift", "release", "shift", "wait", "100",
               "type", "c", "type", "d", "wait", "100", "hold", "shift", "release", "shift",
               "wait", "100", "type", "e", "wait", "300")
            wait_for("typed text", lambda: len(typed()) >= 5)
            assert typed() == "AbCDe", f"twice locks it until pressed again: {typed()!r}"
            print("ok: Sticky Keys: pressed twice, it locks until pressed again")

            vp(*["hold", "shift", "release", "shift"] * 5, "wait", "300", "type", "f", "wait", "300")
            wait_for("Sticky Keys turned off", lambda: "sticky-keys=true" not in settings())
            wait_for("typed text", lambda: len(typed()) >= 6)
            assert typed() == "AbCDef", f"Shift five times turns it off: {typed()!r}"
            print("ok: Shift five times turns Sticky Keys off")

            # ---- Mouse Keys ----
            def presses():
                return [tuple(map(int, l.split()[1:])) for l in lines if l.startswith("press ")]

            vp("home", "move", "200", "200", "click", "wait", "300")
            # Wherever the window is, the click found it or the pointer moves there.
            if not presses():
                raise AssertionError("a click at (200, 200) should land in the client window")
            first = presses()[-1]
            vp("hold", "command", "hold", "shift", "key", "NumLock", "release", "shift",
               "release", "command", "wait", "300")
            wait_for("Mouse Keys on", lambda: "mouse-keys=true" in settings())
            vp("key", "KP6", "key", "KP6", "key", "KP2", "wait", "100", "key", "KP5", "wait", "300")
            wait_for("a keypad click", lambda: len(presses()) >= 2)
            moved = presses()[-1]
            assert moved == (first[0] + 4, first[1] + 2), \
                f"6 moves right, 2 down, 5 clicks: {first} -> {moved}"
            assert typed() == "AbCDef", "the keypad's keys don't reach the program"

            vp("key", "KP0", "wait", "200")
            wait_for("the button held down", lambda: len(presses()) >= 3)
            releases = sum(1 for l in lines if l.startswith("release "))
            vp("key", "KPDot", "wait", "300")
            wait_for("the button let go",
                     lambda: sum(1 for l in lines if l.startswith("release ")) > releases)
            vp("key", "NumLock", "wait", "300")
            wait_for("Mouse Keys off", lambda: "mouse-keys=true" not in settings())
            print("ok: Mouse Keys: the keypad moves the pointer, 5 clicks, 0 holds and . lets go")

            # ---- Zoom ----
            if shutil.which("grim"):
                def shot():
                    path = root / "shot.ppm"
                    subprocess.run(["grim", "-t", "ppm", str(path)], env=env, check=True,
                                   timeout=10)
                    return path.read_bytes()

                before = shot()
                vp("hold", "command", "hold", "option", "key", "k", "release", "option",
                   "release", "command", "wait", "500")
                wait_for("Zoom on", lambda: "zoom=true" in settings())
                zoomed = shot()
                assert zoomed != before, "zooming changes the screen"
                vp("hold", "command", "hold", "option", "key", "k", "release", "option",
                   "release", "command", "wait", "500")
                wait_for("Zoom off", lambda: "zoom=true" not in settings())
                assert shot() == before, "and turning it off puts it back"
                print("ok: ⌘-Option-K zooms the screen and back")
            else:
                print("SKIP: zoom (grim is not installed)")
        finally:
            for p in reversed(processes):
                p.terminate()
            for p in processes:
                try:
                    p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()

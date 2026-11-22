#!/usr/bin/env python3
"""Front Desk with one panel per screen: each screen has its own panel and
its own front program, Command-Tab acts on the focused screen, closing a
program returns only that screen's panel, and dragging a window to the
other screen moves it into that screen's workspace.

usage: test_frontdesk_screens.py ZACOS9-WM MENUBAR IDLE-CLIENT VPTR
"""
import os
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

from zacos9test import wait_for

compositor, menubar, client, vptr = (str(Path(a).resolve()) for a in sys.argv[1:5])
MAGENTA = (0xFF, 0x00, 0xFF)
GREEN = (0x00, 0xAA, 0x00)
ORANGE = (0xFF, 0x88, 0x00)
PANEL = "zacos9-frontdesk"

with tempfile.TemporaryDirectory(prefix="zacos9-frontdesk-screens-") as directory:
    root = Path(directory)
    for d in ("config", "data", "cache", "home"):
        (root / d).mkdir()
    env = dict(os.environ,
               XDG_RUNTIME_DIR=directory, HOME=str(root / "home"),
               XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
               XDG_CACHE_HOME=str(root / "cache"),
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="2",
               ZACOS9_STARTUP="0", ZACOS9_SHELL="frontdesk",
               ZACOS9_MENUBAR_MODE="frontdesk", ZACOS9_MENUBAR=menubar,
               ZACOS9_FRONTDESK_EACH_SCREEN="1",
               ZACOS9_FINDER="", ZACOS9_COLLAR="", ZACOS9_FRONTDESK="", ZACOS9_INHIBIT="",
               QT_QPA_PLATFORM="wayland", QT_STYLE_OVERRIDE="",
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
                subprocess.run([vptr, *args], env=env, check=True, timeout=10)

            def window(app_id, color):
                keys = []
                p = subprocess.Popen([client, "--keys", "--app-id", app_id, "--color", color],
                                     env=env, stdout=subprocess.PIPE, stderr=log, text=True)
                processes.append(p)
                threading.Thread(target=lambda: [keys.append(line.strip()) for line in p.stdout],
                                 daemon=True).start()
                wait_for(f"{app_id} shown", lambda: "shown" in keys)
                return p, keys

            def pixels(output):
                path = root / f"{output}.ppm"
                subprocess.run(["grim", "-o", output, "-t", "ppm", str(path)], env=env,
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
                return data[at + 1:]

            def count(output, color):
                image = pixels(output)
                return sum(tuple(image[i:i + 3]) == color
                           for i in range(0, len(image) - 2, 3 * 7))

            def wait_color(output, what, color, on=True):
                wait_for(what, lambda: (count(output, color) > 0) == on)

            # A panel on each of the two screens (the compositor places the
            # first on one free output and the second on the other).
            vp("home", "move", "100", "100")
            panel1, _ = window(PANEL, "#ff00ff")
            vp("home", "move", "1300", "100")
            panel2, _ = window(PANEL, "#ff00ff")
            wait_color("HEADLESS-1", "the first panel", MAGENTA)
            wait_color("HEADLESS-2", "the second panel", MAGENTA)
            print("ok: a panel on each screen")

            # Start a program on each screen: both draw, one per screen.
            vp("home", "move", "100", "100")
            alpha, _ = window("alpha", "#00aa00")
            wait_color("HEADLESS-2", "Alpha on the left screen", GREEN)
            assert count("HEADLESS-1", MAGENTA) > 0, "the right screen lost its panel"
            vp("home", "move", "1300", "100")
            beta, _ = window("beta", "#ff8800")
            wait_color("HEADLESS-1", "Beta on the right screen", ORANGE)
            assert count("HEADLESS-2", GREEN) > 0, "Alpha vanished when Beta opened"
            print("ok: each screen keeps its own program while the other runs")

            # Command-Tab acts only on the screen it is used from: the left
            # screen swaps its panel and Alpha; the right keeps Beta.
            vp("home", "move", "100", "100", "click")
            wait_for("focus on the left screen", lambda: count("HEADLESS-2", GREEN) > 0)
            vp("cmd", "Tab", "wait", "800")
            wait_color("HEADLESS-2", "the left panel from Command-Tab", MAGENTA)
            assert count("HEADLESS-1", ORANGE) > 0, "Command-Tab disturbed the right screen"
            vp("cmd", "Tab", "wait", "800")
            wait_color("HEADLESS-2", "Alpha from Command-Tab again", GREEN)
            assert count("HEADLESS-1", ORANGE) > 0, "Command-Tab disturbed the right screen"
            print("ok: Command-Tab switches only the focused screen")

            # Closing screen 2's program returns only screen 2's panel.
            beta.terminate()
            beta.wait(timeout=10)
            wait_color("HEADLESS-1", "the right screen's panel after Beta closes", MAGENTA)
            assert count("HEADLESS-2", GREEN) > 0, "the left screen's program was disturbed"
            print("ok: closing a program returns only that screen's panel")

            alpha.terminate()
            alpha.wait(timeout=10)
            wait_color("HEADLESS-2", "the left screen's panel after Alpha closes", MAGENTA)

            # Dragging a program to the other screen moves it into that
            # screen's workspace: the destination panel hides, the source
            # returns.
            vp("home", "move", "100", "100")
            alpha2, _ = window("alpha2", "#00aa00")
            wait_color("HEADLESS-2", "Alpha again on the left screen", GREEN)
            vp("home", "move", "150", "60", "down", "wait", "200",
               "move", "1300", "60", "wait", "300", "up", "wait", "800")
            wait_color("HEADLESS-1", "Alpha on the right screen", GREEN)
            wait_color("HEADLESS-2", "the left panel after Alpha leaves", MAGENTA)
            print("ok: dragging a program moves it to that screen's workspace")
            alpha2.terminate()
            alpha2.wait(timeout=10)
            wait_color("HEADLESS-1", "the right panel after Alpha closes", MAGENTA)
        finally:
            for p in reversed(processes):
                if p.poll() is None:
                    p.terminate()
            for p in reversed(processes):
                try:
                    p.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    p.kill()
print("ok: Front Desk runs one panel and one program per screen")

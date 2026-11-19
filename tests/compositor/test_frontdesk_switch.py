#!/usr/bin/env python3
"""Front Desk keeps the panel in the switchable programs and draws one
program at a time: starting a program hides the panel but leaves it in the
Application menu and ⌘-Tab, choosing it there hides the program, and closing
the last program brings the panel back.

usage: test_frontdesk_switch.py ZACOS9-WM MENUBAR IDLE-CLIENT VPTR
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
BLUE = (0xFF, 0x00, 0xFF)  # the panel's stand-in colour: no UI uses it
GREEN = (0x00, 0xAA, 0x00)
PANEL = "zacos9-frontdesk"

with tempfile.TemporaryDirectory(prefix="zacos9-frontdesk-switch-") as directory:
    root = Path(directory)
    for d in ("config", "data", "cache", "home"):
        (root / d).mkdir()
    env = dict(os.environ,
               XDG_RUNTIME_DIR=directory, HOME=str(root / "home"),
               XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
               XDG_CACHE_HOME=str(root / "cache"),
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
               ZACOS9_STARTUP="0", ZACOS9_SHELL="frontdesk",
               ZACOS9_MENUBAR_MODE="frontdesk", ZACOS9_MENUBAR=menubar,
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

            def pixels():
                path = root / "shot.ppm"
                subprocess.run(["grim", "-t", "ppm", str(path)], env=env, check=True, timeout=10)
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

            def count(color):
                image = pixels()
                return sum(tuple(image[i:i + 3]) == color
                           for i in range(0, len(image) - 2, 3 * 7))

            def wait_color(what, color, on=True):
                wait_for(what, lambda: (count(color) > 0) == on)

            def typed(letter, keys):
                vp("type", letter)
                wait_for(f"the key {letter}", lambda: f"key {letter}" in keys)

            # The panel stands in for zacos9-frontdesk: the same app id, a
            # magenta window; Alpha is a green program on top of it.
            panel, panel_keys = window(PANEL, "#ff00ff")
            wait_for("the panel", lambda: count(BLUE) > 0)
            alpha, alpha_keys = window("alpha", "#00aa00")
            wait_for("Alpha", lambda: count(GREEN) > 0)

            # Alpha is in front: the panel is hidden but still a program (it
            # stays mapped), and Alpha hides when the panel comes back.
            wait_color("the panel to hide behind Alpha", BLUE, on=False)
            assert count(GREEN) > 0, "Alpha is the one program drawn"
            typed("a", alpha_keys)
            print("ok: starting a program hides the Front Desk panel")

            # The top-right Application menu lists Front Desk under Alpha;
            # choosing it hides Alpha and brings the panel back.
            vp("home", "move", "1250", "9", "down", "wait", "300", "move", "-40", "89",
               "wait", "300", "up", "wait", "800")
            wait_color("Front Desk from the Application menu", BLUE)
            assert count(GREEN) == 0, "Alpha hides while the panel is in front"
            typed("b", panel_keys)
            print("ok: the Application menu brings Front Desk back and hides the program")

            # ⌘-Tab goes round the panel and Alpha only: no Sniffer in a
            # Front Desk session, and the other program is hidden again.
            vp("cmd", "Tab", "wait", "800")
            wait_color("Alpha from Command-Tab", GREEN)
            assert count(BLUE) == 0, "the panel hides behind Alpha again"
            typed("c", alpha_keys)
            print("ok: Command-Tab switches back to the program, one at a time")

            # Closing the last program returns to the panel.
            alpha.terminate()
            alpha.wait(timeout=10)
            wait_color("the panel after Alpha exits", BLUE)
            assert count(GREEN) == 0, "the closed program is gone"
            typed("d", panel_keys)
            print("ok: closing the last program returns to Front Desk")
        finally:
            for p in reversed(processes):
                if p.poll() is None:
                    p.terminate()
            for p in reversed(processes):
                try:
                    p.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    p.kill()
print("ok: Front Desk switches among programs and shows one at a time")

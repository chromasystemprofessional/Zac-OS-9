"""Keyboard layouts (compositor/src/input.c, kblayout.c; the menu bar's
keyboard menu, shell/menubar/kbmenu.c): ⌘-Space steps through the layouts
ticked in the Keyboard panel, ⇧ backwards, or ⌘-Option-Space when the panel
says; the one in use is remembered; the keyboard menu shows it, and goes
when only one layout is left.

usage: test_keyboard_layouts.py ZACOS9-WM MENUBAR VPTR
"""
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from zacos9test import wait_for

compositor, menubar, vptr = (str(Path(a).resolve()) for a in sys.argv[1:4])
if not shutil.which("grim"):
    print("SKIP: grim is not installed")
    sys.exit(77)


with tempfile.TemporaryDirectory(prefix="zacos9-kb-") as directory:
    root = Path(directory)
    (root / "config" / "zacos9").mkdir(parents=True)
    # The clock off: a minute ticking over mid-test would keep the bar
    # from ever looking as it did (it did once, in a package build).
    conf = root / "config" / "zacos9" / "desktop.conf"
    conf.write_text("[General]\nclock=off\nkeyboard-layouts=us,us(dvorak)\n")
    (root / "keyboard").write_text('XKBMODEL="pc105"\nXKBLAYOUT="us"\n')
    remembered = root / "state" / "zacos9" / "keyboard-layout"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, HOME=directory,
               XDG_CONFIG_HOME=str(root / "config"), XDG_STATE_HOME=str(root / "state"),
               ZACOS9_KEYBOARD_DEFAULTS=str(root / "keyboard"),
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
               ZACOS9_STARTUP="0", ZACOS9_MENUBAR=shlex.quote(menubar), ZACOS9_FINDER="",
               ZACOS9_COLLAR="", ZACOS9_INHIBIT="")
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

            def bar():
                """The menu bar's right end: the clock, keyboard and Application menus."""
                path = root / "bar.ppm"
                subprocess.run(["grim", "-g", "880,0 400x20", "-t", "ppm", str(path)],
                               env=env, check=True, timeout=10)
                return path.read_bytes()

            def layout():
                return remembered.read_text().strip() if remembered.exists() else None

            def settle():
                """The bar, once it has stopped changing (the clock ticks by minutes)."""
                last = bar()
                while True:
                    time.sleep(0.4)
                    now = bar()
                    if now == last:
                        return now
                    last = now

            def drawn():
                """The menu bar is up: its #DDDDDD face fills most of the strip."""
                data = bar()
                pixels = data[data.index(b"255\n") + 4:]
                face = sum(pixels[i:i + 3] == b"\xdd\xdd\xdd" for i in range(0, len(pixels) - 2, 3))
                return face > len(pixels) // 3 // 2

            wait_for("the menu bar", drawn)
            us = settle()

            vp("cmd", " ", "wait", "300")
            wait_for("Dvorak", lambda: layout() == "us(dvorak)")
            wait_for("the keyboard menu to show it", lambda: bar() != us)
            dvorak = settle()
            vp("cmd", " ", "wait", "300")
            wait_for("U.S. again", lambda: layout() == "us")
            wait_for("the keyboard menu back", lambda: bar() == us)
            print("ok: ⌘-Space steps through the layouts; the keyboard menu shows the one in use")

            # Three layouts, changed while running: ⇧⌘-Space goes backwards.
            conf.write_text("[General]\nclock=off\nkeyboard-layouts=us,us(dvorak),gb\n")
            time.sleep(0.5)
            vp("hold", "shift", "cmd", " ", "release", "shift", "wait", "300")
            wait_for("U.K., the last", lambda: layout() == "gb")
            print("ok: a change in the Keyboard panel applies at once; ⇧⌘-Space goes back")

            # ⌘-Option-Space instead: plain ⌘-Space is the program's.
            conf.write_text("[General]\nclock=off\nkeyboard-layouts=us,us(dvorak),gb\nkeyboard-switch=option\n")
            time.sleep(0.5)
            vp("cmd", " ", "wait", "400")
            assert layout() == "gb", "⌘-Space no longer switches"
            vp("hold", "option", "cmd", " ", "release", "option", "wait", "300")
            wait_for("U.S. after U.K.", lambda: layout() == "us")
            print("ok: the Keyboard panel can make it ⌘-Option-Space")

            # One layout: no keyboard menu, and ⌘-Space is left alone.
            conf.write_text("[General]\nclock=off\nkeyboard-layouts=gb\n")
            wait_for("the keyboard menu to go", lambda: bar() not in (us, dvorak))
            single = settle()
            vp("hold", "option", "cmd", " ", "release", "option", "wait", "400")
            assert layout() == "us" and bar() == single, "nothing to switch to"
            print("ok: with one layout there is no keyboard menu")
        finally:
            for p in reversed(processes):
                p.terminate()
            for p in processes:
                try:
                    p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()

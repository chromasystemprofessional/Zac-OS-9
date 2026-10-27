"""Mac OS 8/9 pop-up windows (compositor/src/view.c, input.c, decor.c; the
Sniffer's View > as Pop-up Window): a Sniffer window becomes a tab along the
bottom of the screen. Clicking the tab opens it above the tab and clicking
it again, or anywhere else, closes it; dragging the tab along moves it;
a drag resting on the tab springs it open for the drop; dragging the tab up
makes it an ordinary window again. The Sniffer remembers all of it.

usage: test_popup_windows.py ZACOS9-WM MENUBAR FINDER VPTR
"""
import configparser
import os
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from zacos9test import wait_for

compositor, menubar, finder, vptr = (str(Path(a).resolve()) for a in sys.argv[1:5])
if not shutil.which("grim"):
    print("SKIP: grim is not installed")
    sys.exit(77)
FACE = (204, 204, 204)   # an active tab's face
WHITE = (255, 255, 255)  # a folder window's background
TAB_Y = 710              # inside a closed tab, on a 1280x720 screen
BODY = (100, 600)        # inside an open pop-up's body (420x280 by default)


with tempfile.TemporaryDirectory(prefix="zacos9-popup-") as directory:
    root = Path(directory)
    home = root / "home"
    for d in ("config", "data", "cache", "home/Desktop", "home/Work", "home/Other"):
        (root / d).mkdir(parents=True)
    (home / "Work/a.txt").write_text("a")
    (home / "Other/memo.txt").write_text("memo")
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, HOME=str(home),
               XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
               XDG_CACHE_HOME=str(root / "cache"),
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
               ZACOS9_STARTUP="0", ZACOS9_COLLAR="", ZACOS9_INHIBIT="", ZACOS9_MENUBAR=shlex.quote(menubar),
               ZACOS9_FINDER=shlex.quote(finder), QT_QPA_PLATFORM="wayland", QT_STYLE_OVERRIDE="",
               QT_WAYLAND_DISABLE_WINDOWDECORATION="1")
    env.pop("WAYLAND_DISPLAY", None)
    folders = root / "data/zacos9/finder/folders.ini"
    processes = []
    with (root / "compositor.log").open("w+") as log:
        try:
            server = subprocess.Popen([compositor], env=env, stdout=log, stderr=log)
            processes.append(server)
            sockets = wait_for("the compositor", lambda: server.poll() is None and [
                p for p in root.glob("wayland-*") if not p.name.endswith(".lock")])
            env["WAYLAND_DISPLAY"] = sockets[0].name
            sniffer = root / f"zacos9-finder.{sockets[0].name}.sock"
            wait_for("the Sniffer", sniffer.exists, 30)

            def vp(*args):
                subprocess.run([vptr, *args], env=env, check=True, timeout=20)

            def cmd(line):
                with socket.socket(socket.AF_UNIX) as s:
                    s.connect(str(sniffer))
                    s.sendall(f"cmd {line}\n".encode())
                    time.sleep(1.0)

            def state():
                """The Sniffer's state line, as the menu bar reads it."""
                with socket.socket(socket.AF_UNIX) as s:
                    s.connect(str(sniffer))
                    s.settimeout(5)
                    line = s.recv(4096).decode().splitlines()[0]
                return dict(f.split("=", 1) for f in line.split()[1:])

            def saved(key):
                config = configparser.ConfigParser(interpolation=None)
                config.read(folders)
                for section in config.sections():
                    if config[section].get("path", "").rstrip("/") == str(home / "Work"):
                        return config[section].get(key)
                return None

            def shot():
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
                w = fields[0]
                pixels = data[at + 1:]
                return lambda x, y: tuple(pixels[(y * w + x) * 3:(y * w + x) * 3 + 3])

            def is_open():
                return shot()(*BODY) == WHITE

            vp("key", "Escape")
            cmd(f"open-path {home}/Work")
            assert state()["popups"] == "1", "zacos9-wm offers pop-up windows"
            assert state()["popup"] == "0", "a new window is a window"

            # View > as Pop-up Window: the window in front stays open, now
            # standing on its tab at the bottom of the screen.
            cmd("view-popup")
            assert state()["popup"] == "1", "View > as Pop-up Window is checked"
            wait_for("the pop-up open above its tab", is_open, 5)
            wait_for("the pop-up remembered", lambda: saved("popup") == "true", 5)
            print("ok: View > as Pop-up Window docks the window on a tab, open")

            # A click anywhere else closes it; on its tab, opens and closes it.
            vp("home", "move", "640", "250", "click", "wait", "500")
            wait_for("the pop-up closed", lambda: not is_open(), 5)
            assert shot()(20, TAB_Y) == FACE, "the closed pop-up leaves its tab"
            vp("home", "move", "20", str(TAB_Y), "click", "wait", "700")
            assert is_open(), "a click on the tab opens it"
            # Open, the tab stands on top of the body.
            vp("home", "move", "20", "420", "click", "wait", "700")
            assert not is_open(), "a click on the open tab closes it"
            print("ok: the tab opens and closes it, and a click elsewhere closes it")

            # The tab dragged along the bottom of the screen moves there.
            vp("home", "move", "20", str(TAB_Y), "down", "wait", "100", "move", "100", "0", "wait", "100",
               "move", "200", "0", "wait", "300", "up", "wait", "700")
            px = shot()
            assert px(330, TAB_Y) == FACE and px(10, TAB_Y) != FACE, "the tab moved along"
            wait_for("the tab's place remembered", lambda: int(saved("tab-x") or -1) > 250, 5)
            print("ok: dragging the tab along the bottom moves it, and it is remembered")

            # Closed and opened again (as at the next login): still a pop-up.
            vp("home", "move", "330", str(TAB_Y), "click", "wait", "700")
            cmd("close-window")
            wait_for("Work closed", lambda: shot()(330, TAB_Y) != FACE, 5)
            cmd(f"open-path {home}/Work")
            wait_for("Work back as a pop-up, open", lambda: shot()(330, 600) == WHITE, 5)
            assert state()["popup"] == "1"
            print("ok: a pop-up comes back as a pop-up, its tab where it was")

            # A drag resting on the closed tab springs it open for the drop,
            # and it closes again after. Other opens at the top left, memo
            # its first icon.
            cmd(f"open-path {home}/Other")
            wait_for("the pop-up closed behind Other", lambda: shot()(340, 600) != WHITE, 5)
            vp("home", "move", "133", "150", "down", "wait", "100", "move", "10", "10", "wait", "200",
               "move", "100", "300", "wait", "200", "move", "97", "250", "wait", "1300")
            assert shot()(340, 600) == WHITE, "a drag resting on the tab opens the pop-up"
            vp("move", "0", "-150", "wait", "400", "up", "wait", "500")
            wait_for("memo moved into Work", (home / "Work/memo.txt").exists)
            wait_for("the pop-up closed again", lambda: shot()(340, 600) != WHITE, 5)
            print("ok: a drag resting on a pop-up's tab springs it open, then it closes")

            # Its tab dragged up: an ordinary window again, where it lands.
            vp("home", "move", "340", str(TAB_Y), "down", "wait", "100", "move", "0", "-100", "wait", "200",
               "move", "0", "-200", "wait", "300", "up", "wait", "800")
            assert state()["popup"] == "0", "View > as Window is checked again"
            assert shot()(340, TAB_Y) != FACE, "no tab left at the bottom"
            wait_for("the window remembered", lambda: saved("popup") == "false", 5)
            print("ok: dragging the tab up makes it an ordinary window")
        finally:
            for p in reversed(processes):
                p.terminate()
            for p in processes:
                try:
                    p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()

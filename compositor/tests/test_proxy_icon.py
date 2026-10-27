"""Mac OS 9's proxy icon (compositor/src/decor.c, input.c; the Sniffer's
folder windows): the folder's small icon in a Sniffer window's title bar;
dragging it moves the folder, as dragging its icon would. And a drag resting
on a collapsed window's title bar springs it open for the drop, closing it
again afterwards.

usage: test_proxy_icon.py ZACOS9-WM MENUBAR FINDER VPTR
"""
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
FOLDER = (204, 204, 255)  # the small folder icon's lavender


with tempfile.TemporaryDirectory(prefix="zacos9-proxy-") as directory:
    root = Path(directory)
    home = root / "home"
    for d in ("config", "data", "cache", "home/Desktop", "home/Work/Projects"):
        (root / d).mkdir(parents=True)
    (home / "Work/Projects/a.txt").write_text("a")
    (home / "Work/memo.txt").write_text("memo")
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, HOME=str(home),
               XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
               XDG_CACHE_HOME=str(root / "cache"),
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
               ZACOS9_STARTUP="0", ZACOS9_COLLAR="", ZACOS9_INHIBIT="", ZACOS9_MENUBAR=shlex.quote(menubar),
               ZACOS9_FINDER=shlex.quote(finder), QT_QPA_PLATFORM="wayland", QT_STYLE_OVERRIDE="",
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
            sniffer = root / f"zacos9-finder.{sockets[0].name}.sock"
            wait_for("the Sniffer", sniffer.exists, 30)

            def vp(*args):
                subprocess.run([vptr, *args], env=env, check=True, timeout=20)

            def cmd(line):
                with socket.socket(socket.AF_UNIX) as s:
                    s.connect(str(sniffer))
                    s.sendall(f"cmd {line}\n".encode())
                    time.sleep(1.0)

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

            # Projects opens first, at the top left; collapsed by its box.
            # Work opens below and right of it, and is moved further down.
            vp("key", "Escape")
            cmd(f"open-path {home}/Work/Projects")
            vp("home", "move", "460", "50", "click", "wait", "500")
            cmd(f"open-path {home}/Work")
            vp("home", "move", "380", "70", "down", "move", "0", "150", "move", "0", "150", "up", "wait", "500")
            px = shot()
            assert px(100, 200) != (255, 255, 255), "Projects is collapsed"

            # memo dragged to Projects' title bar and held: it springs open.
            vp("home", "move", "113", "428", "down", "wait", "100", "move", "10", "-10", "wait", "200",
               "move", "100", "-150", "wait", "200", "move", "77", "-218", "wait", "1300")
            assert shot()(100, 200) == (255, 255, 255), "a drag resting on its title bar opens it"
            vp("move", "0", "100", "wait", "400", "up", "wait", "500")
            wait_for("memo moved into Projects", (home / "Work/Projects/memo.txt").exists)
            wait_for("Projects collapsed again", lambda: shot()(100, 200) != (255, 255, 255), 5)
            print("ok: a drag resting on a collapsed window's title bar springs it open, then it closes")

            # The proxy icon: found by its colour in Projects' title bar.
            px = shot()
            icon = [(x, y) for y in range(42, 59) for x in range(40, 470) if px(x, y) == FOLDER]
            assert icon, "Projects has a folder icon in its title bar"
            ix = sum(x for x, _ in icon) // len(icon)
            iy = sum(y for _, y in icon) // len(icon)
            vp("home", "move", str(ix), str(iy), "down", "wait", "100", "move", "10", "10", "wait", "300",
               "move", "200", "150", "wait", "200", "move", "400", "150", "wait", "400", "up", "wait", "500")
            wait_for("Projects moved to the desktop", (home / "Desktop/Projects/memo.txt").exists)
            assert not (home / "Work/Projects").exists(), "moved, not copied"
            print("ok: dragging a window's proxy icon moves its folder")
        finally:
            for p in reversed(processes):
                p.terminate()
            for p in processes:
                try:
                    p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()

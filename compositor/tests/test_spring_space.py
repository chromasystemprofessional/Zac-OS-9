#!/usr/bin/env python3
"""Space during a drag springs the folder under it open at once.

Headless zacos9-wm with the Sniffer, the spring-loaded delay at its
longest (3 s): memo dragged onto the Docs folder beside it, in the
Other window, doesn't open it in the first moments; Space does, straight
away (zacos9-wm sends platinum-shell-v1 drag_spring, as programs get no
keys during a drag); the drop then lands in Docs.

usage: test_spring_space.py ZACOS9-WM MENUBAR FINDER VPTR
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
DOCS = (94, 108)      # Other's icons, its window at the top left
MEMO = (174, 108)


with tempfile.TemporaryDirectory(prefix="zacos9-spring-") as directory:
    root = Path(directory)
    home = root / "home"
    for d in ("config/zacos9", "data", "cache", "home/Desktop", "home/Other/Docs"):
        (root / d).mkdir(parents=True)
    (home / "Other/memo.txt").write_text("memo")
    (root / "config/zacos9/desktop.conf").write_text("spring-delay=3000\n")
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

            def shot():
                path = root / "shot.ppm"
                subprocess.run(["grim", "-t", "ppm", str(path)], env=env, check=True, timeout=10)
                return path.read_bytes()

            def changed(a, b):
                return sum(1 for x, y in zip(a, b) if x != y)

            vp("key", "Escape")
            with socket.socket(socket.AF_UNIX) as s:
                s.connect(str(sniffer))
                s.sendall(f"cmd open-path {home}/Other\n".encode())
                time.sleep(1.5)
            vp("home", "move", str(MEMO[0]), str(MEMO[1]), "down", "move", "0", "20", "wait", "100",
               "move", str(DOCS[0] - MEMO[0]), "-20", "wait", "400")
            resting = shot()
            vp("wait", "400")
            assert changed(resting, shot()) < 2000, "the delay hasn't passed: nothing springs open yet"
            vp("key", " ", "wait", "600")
            assert changed(resting, shot()) > 20000, "Space springs Docs open at once"
            print("ok: Space during a drag springs the folder open without the delay")

            vp("up", "wait", "1000")
            wait_for("memo moved into Docs", (home / "Other/Docs/memo.txt").exists)
            print("ok: the drop lands in Docs")
        finally:
            for p in reversed(processes):
                p.terminate()
            for p in processes:
                try:
                    p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()

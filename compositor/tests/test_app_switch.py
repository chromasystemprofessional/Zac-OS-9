"""⌘-Tab and Option-click on the desktop (compositor/src/input.c, view.c and
the menu bar's app_switch_target), then Special > Log Out (the Sniffer asks,
the menu bar quits the programs: shell/menubar/logout.c), with the real menu
bar and Sniffer.

usage: test_app_switch.py ZACOS9-WM MENUBAR FINDER IDLE-CLIENT VPTR
"""
import os
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

compositor, menubar, finder, client, vptr = (str(Path(a).resolve()) for a in sys.argv[1:6])
GREEN = (0x00, 0xAA, 0x00)


def wait_for(what, check, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError(f"timed out waiting for {what}")


with tempfile.TemporaryDirectory(prefix="zacos9-switch-") as directory:
    root = Path(directory)
    for d in ("config", "data", "cache", "home"):
        (root / d).mkdir()
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, HOME=str(root / "home"),
               XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
               XDG_CACHE_HOME=str(root / "cache"),
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
               ZACOS9_STARTUP="0", ZACOS9_COLLAR="", ZACOS9_INHIBIT="",
               ZACOS9_MENUBAR=shlex.quote(menubar), ZACOS9_FINDER=shlex.quote(finder),
               QT_QPA_PLATFORM="wayland", QT_STYLE_OVERRIDE="",
               QT_WAYLAND_DISABLE_WINDOWDECORATION="1",
               ZACOS9_LOGOUT_COMMAND=f"touch {shlex.quote(str(root / 'logged-out'))}")
    env.pop("WAYLAND_DISPLAY", None)
    processes = []
    with (root / "compositor.log").open("w+") as log:
        try:
            server = subprocess.Popen([compositor], env=env, stdout=log, stderr=log)
            processes.append(server)
            sockets = wait_for("the compositor", lambda: server.poll() is None and [
                p for p in root.glob("wayland-*") if not p.name.endswith(".lock")])
            env["WAYLAND_DISPLAY"] = sockets[0].name
            # The Sniffer's desktop is up once it answers on its socket.
            wait_for("the Sniffer", lambda: (root / f"zacos9-finder.{sockets[0].name}.sock").exists(), 30)

            def vp(*args):
                subprocess.run([vptr, *args], env=env, check=True, timeout=10)

            keys = {}

            def window(app_id):
                p = subprocess.Popen([client, "--keys", "--app-id", app_id], env=env,
                                     stdout=subprocess.PIPE, stderr=log, text=True)
                processes.append(p)
                keys[app_id] = []
                threading.Thread(target=lambda: [keys[app_id].append(l.strip()) for l in p.stdout],
                                 daemon=True).start()
                wait_for(f"{app_id} shown", lambda: "shown" in keys[app_id])

            def typed(letter):
                """Which window, if any, got `letter`."""
                vp("type", letter, "wait", "300")
                got = [a for a, k in keys.items() if f"key {letter}" in k]
                assert len(got) <= 1, got
                return got[0] if got else None

            def switch(letter):
                vp("cmd", "Tab", "wait", "300")
                return typed(letter)

            have_grim = shutil.which("grim") is not None

            def green():
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
                pixels = data[at + 1:]
                return sum(tuple(pixels[i:i + 3]) == GREEN for i in range(0, len(pixels) - 2, 3 * 7))

            window("alpha")
            window("bravo")
            assert typed("s") == "bravo", "the newest window is in front"

            # By name: Alpha, Bravo, Sniffer. From Bravo, ⌘-Tab goes to the Sniffer
            # (no windows: keys go to the desktop), then round to Alpha and Bravo.
            assert switch("t") is None, "⌘-Tab from Bravo brings the Sniffer forward"
            assert switch("u") == "alpha", "then Alpha"
            assert switch("v") == "bravo", "then Bravo"
            print("ok: ⌘-Tab goes through the programs by name, the Sniffer among them")

            # Option-click on the desktop: Bravo, the program left, hides. New
            # windows open in the same place; Bravo moves down first, so hiding
            # it shows less green.
            vp("home", "move", "640", "62", "down", "move", "0", "250", "up", "wait", "400")
            before = green() if have_grim else None
            vp("home", "move", "40", "400", "hold", "option", "click", "release", "option", "wait", "400")
            assert typed("w") is None, "Option-click on the desktop brings the Sniffer forward"
            if have_grim:
                assert green() < before, "and hides the program you were in"
            assert switch("x") == "alpha", "⌘-Tab from the Sniffer goes to Alpha"
            assert switch("y") == "bravo", "and a hidden program comes back with ⌘-Tab"
            if have_grim:
                # (the pointer, over the window in the first shot, is on the desktop now)
                assert abs(green() - before) <= before // 100, "showing its window again"
            print("ok: Option-click on the desktop hides the program you leave")

            # Special > Log Out: the Sniffer asks; Return says Log Out; the
            # programs are asked to quit, and once they have, the session ends.
            with socket.socket(socket.AF_UNIX) as s:
                s.connect(str(root / f"zacos9-finder.{sockets[0].name}.sock"))
                s.sendall(b"cmd log-out\n")
                time.sleep(1.5)
                assert all(p.poll() is None for p in processes), "nothing quits before you answer"
                vp("key", "Return")
                wait_for("the programs to quit", lambda: all(p.poll() is not None for p in processes[1:]))
                wait_for("the session to end", (root / "logged-out").exists)
            print("ok: Log Out quits the programs, then ends the session")
        finally:
            for p in reversed(processes):
                p.terminate()
            for p in processes:
                try:
                    p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()

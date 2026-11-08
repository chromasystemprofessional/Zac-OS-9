"""Global application shortcuts (compositor/src/input.c, view.c): ⌘Q quits
the program in front, ⌘H hides it, and ⌘M collapses (or expands) its front
window. The Sniffer keeps those keys for its own File menu and cannot be
quit, as the Mac's Finder couldn't.

usage: test_app_shortcuts.py ZACOS9-WM IDLE-CLIENT VPTR
"""
import os
import shutil
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

from zacos9test import wait_for

compositor, client, vptr = (str(Path(a).resolve()) for a in sys.argv[1:4])
if not shutil.which("grim"):
    print("SKIP: grim is not installed")
    sys.exit(77)
GREEN = (0x00, 0xAA, 0x00)


with tempfile.TemporaryDirectory(prefix="zacos9-app-shortcuts-") as directory:
    root = Path(directory)
    for d in ("home", "data", "cache"):
        (root / d).mkdir()
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, HOME=str(root / "home"),
               XDG_CONFIG_HOME=directory, XDG_DATA_HOME=str(root / "data"),
               XDG_CACHE_HOME=str(root / "cache"),
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
               ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="", ZACOS9_COLLAR="",
               ZACOS9_INHIBIT="", ZACOS9_DISK_HELPER="",
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

            def window(app_id):
                keys = []
                p = subprocess.Popen([client, "--keys", "--app-id", app_id], env=env,
                                     stdout=subprocess.PIPE, stderr=log, text=True)
                processes.append(p)
                threading.Thread(target=lambda: [keys.append(l.strip()) for l in p.stdout],
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

            def green():
                image = pixels()
                return sum(tuple(image[i:i + 3]) == GREEN for i in range(0, len(image) - 2, 3 * 7))

            # The first key of the session's first virtual keyboard goes on
            # its keymap (Escape, which does nothing here).
            vp("key", "Escape", "wait", "300")

            alpha, alpha_keys = window("alpha")
            bravo, bravo_keys = window("bravo")

            # ⌘Q closes every window of the program in front; the one behind
            # keeps its window and the keyboard.
            vp("cmd", "q", "wait", "800")
            wait_for("Bravo to quit", lambda: bravo.poll() is not None)
            assert alpha.poll() is None, "⌘Q left the program behind it alone"
            vp("type", "q", "wait", "300")
            wait_for("Alpha to get the keyboard", lambda: "key q" in alpha_keys)
            print("ok: ⌘Q quits the program in front, not the one behind it")

            # ⌘H hides the program in front; its windows stay (the process
            # runs), but the keyboard goes to the program behind.
            bravo, bravo_keys = window("bravo")
            vp("cmd", "h", "wait", "600")
            assert bravo.poll() is None, "⌘H quits rather than hides"
            vp("type", "h", "wait", "300")
            wait_for("Alpha to get the keyboard", lambda: "key h" in alpha_keys)
            assert "key h" not in bravo_keys, "the hidden program still gets the keyboard"
            print("ok: ⌘H hides the program in front")

            # ⌘M collapses the front window to its title bar, and again
            # expands it. (Bravo is hidden; Alpha is in front.)
            before = green()
            vp("cmd", "m", "wait", "600")
            collapsed = green()
            assert collapsed < before * 0.1, (before, collapsed)
            vp("cmd", "m", "wait", "600")
            assert green() >= before * 0.95, "⌘M again expands the window"
            print("ok: ⌘M collapses the front window, and again expands it")

            # The Sniffer keeps ⌘Q, ⌘H and ⌘M for its own File menu (⌘H
            # Search Internet, ⌘M Make Alias) and cannot be quit.
            finder, _ = window("zacos9-finder")
            before = green()
            vp("cmd", "q", "wait", "400")
            assert finder.poll() is None, "the Sniffer was quit"
            vp("cmd", "h", "wait", "400")
            assert finder.poll() is None and green() >= before * 0.95, "the Sniffer was hidden"
            vp("cmd", "m", "wait", "400")
            assert green() >= before * 0.95, "the Sniffer's ⌘M was swallowed"
            print("ok: the Sniffer keeps ⌘Q, ⌘H and ⌘M for its own commands")
        finally:
            for p in reversed(processes):
                if p.poll() is None:
                    p.terminate()
            for p in processes:
                try:
                    p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()

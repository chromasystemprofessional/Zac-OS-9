"""Window tricks from Mac OS 9 (compositor/src/input.c): Option-click on the
close box closes all of the program's windows, on the collapse box collapses
only the program's; ⌘-dragging a window behind moves it without bringing it
forward. General Controls' "Show Desktop when in background", off: the
Sniffer's windows hide while another program is in front, and come back
when nothing else is left.

usage: test_window_tricks.py ZACOS9-WM IDLE-CLIENT VPTR CLOSE-APP-CLIENT
"""
import os
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

compositor, client, vptr, close_app_client = (str(Path(a).resolve()) for a in sys.argv[1:5])
GREEN = (0x00, 0xAA, 0x00)
# Other programs' windows open at the same place on a 1280 x 720 screen with
# no menu bar: the frame's top-left is (97, 32); its boxes sit on the title
# bar's row, y = 42.
CLOSE, COLLAPSE, TITLE = (106, 42), (1172, 42), (640, 42)


def wait_for(what, check, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError(f"timed out waiting for {what}")


with tempfile.TemporaryDirectory(prefix="zacos9-tricks-") as directory:
    root = Path(directory)
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_CONFIG_HOME=directory,
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
                subprocess.run([vptr, *args], env=env, check=True, timeout=10)

            def click(at, *mods):
                held = [a for m in mods for a in ("hold", m)]
                released = [a for m in mods for a in ("release", m)]
                vp("home", "move", str(at[0]), str(at[1]), *held, "click", *released, "wait", "500")

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

            def window(app_id):
                keys = []
                p = subprocess.Popen([client, "--keys", "--app-id", app_id], env=env,
                                     stdout=subprocess.PIPE, stderr=log, text=True)
                processes.append(p)
                threading.Thread(target=lambda: [keys.append(l.strip()) for l in p.stdout],
                                 daemon=True).start()
                wait_for(f"{app_id} shown", lambda: "shown" in keys)
                return p, keys

            # Title-bar double-click collapse can be disabled, and stays enabled
            # by default when the preference is absent.
            config = root / "zacos9"
            config.mkdir(exist_ok=True)
            settings = config / "desktop.conf"
            settings.write_text("[General]\ndouble-click-title-collapse=false\n")
            a1, _ = window("alpha")
            before = green()
            vp("home", "move", *map(str, TITLE), "click", "click", "wait", "500")
            assert green() >= before * 0.95, "disabled title-bar collapse leaves the window open"
            settings.write_text("[General]\n")
            time.sleep(0.1)
            before = green()
            vp("home", "move", *map(str, TITLE), "click", "click", "wait", "500")
            assert green() < before * 0.9, "default title-bar double-click collapses the window"
            vp("home", "move", *map(str, TITLE), "click", "click", "wait", "500")
            assert green() >= before * 0.95, "a second double-click restores the window"
            print("ok: title-bar double-click collapse preference")

            # Two windows of one program (Alpha), one of another (Bravo) in front.
            # All open at the same place; Bravo moves down so Alpha's title shows.
            a2, a2_keys = window("alpha")
            b, b_keys = window("bravo")
            subprocess.run([close_app_client, "alpha"], env=env, check=True, timeout=10)
            wait_for("the front Alpha window to close", lambda: a2.poll() is not None)
            assert a1.poll() is None, "closing an app window leaves its other windows open"
            print("ok: close-app-window closes only the front window of the selected app")
            vp("home", "move", *map(str, TITLE), "down", "move", "0", "250", "up", "wait", "500")
            dy = 250

            # ⌘-drag Alpha's front window, behind Bravo: it moves (its title bar
            # now crosses Alpha's other window), and Bravo keeps the keyboard.
            before = green()
            vp("home", "move", *map(str, TITLE), "hold", "command", "down", "move", "0", "100",
               "up", "release", "command", "wait", "500")
            assert green() < before, "the window behind moved"
            vp("type", "k")
            wait_for("Bravo, still in front, to get the key", lambda: "key k" in b_keys)
            assert "key k" not in a2_keys, "and not the window moved behind it"
            # The same move gesture works from the window's border, not just
            # its title bar. Alpha is now 100 px below its original position.
            before = pixels()
            vp("home", "move", "99", "232", "hold", "command", "down", "move", "100", "0",
               "up", "release", "command", "wait", "500")
            after = pixels()
            assert sum(a != b for a, b in zip(before, after)) > 1000, \
                "the window behind moves when dragged by its edge"
            vp("type", "e")
            wait_for("Bravo, still in front, to receive the edge-drag key", lambda: "key e" in b_keys)
            assert "key e" not in a2_keys
            vp("home", "move", "199", "232", "hold", "command", "down", "move", "-100", "0",
               "up", "release", "command", "wait", "500")
            print("ok: ⌘-dragging a background window by title or edge moves it without raising it")

            # Option-click on Alpha's collapse box: both of Alpha's windows roll
            # up, Bravo's doesn't (Mac OS 9: "all windows in the current program").
            # Boxes show on the front window only: Alpha's comes forward first.
            click((TITLE[0], TITLE[1] + 100))
            before = green()
            click((COLLAPSE[0], COLLAPSE[1] + 100), "option")
            after = green()
            assert before * 0.3 < after < before * 0.9, (before, after)
            click((COLLAPSE[0], COLLAPSE[1] + 100), "option")  # and back
            assert green() >= before * 0.95, "Option-click again unrolls them"
            print("ok: Option-click on the collapse box is for the program's own windows")

            # Option-click on Alpha's close box closes both of Alpha's windows,
            # not Bravo's; then Bravo's.
            click((CLOSE[0], CLOSE[1] + 100), "option")
            wait_for("both of Alpha's windows to close",
                     lambda: a1.poll() is not None and a2.poll() is not None)
            assert b.poll() is None, "Bravo's window stays"
            click((CLOSE[0], CLOSE[1] + dy), "option")
            wait_for("Bravo to quit", lambda: b.poll() is not None)
            print("ok: Option-click on the close box closes all of the program's windows")

            # Show Desktop when in background, off (read as it changes). A
            # stand-in Sniffer window, then another program's over it, moved
            # down: the Sniffer's isn't there to click.
            settings.write_text("[General]\nshow-desktop=false\n")
            sniffer, sniffer_keys = window("zacos9-finder")
            alone = green()
            b, b_keys = window("bravo")
            vp("home", "move", *map(str, TITLE), "down", "move", "0", "250", "up", "wait", "500")
            click(TITLE)
            vp("type", "q")
            wait_for("Bravo to keep the keyboard", lambda: "key q" in b_keys)
            assert "key q" not in sniffer_keys, "the Sniffer's window hid behind Bravo"
            # Bravo quits: nothing else is open, so the Sniffer's comes back.
            click((CLOSE[0], CLOSE[1] + dy))
            wait_for("Bravo to quit", lambda: b.poll() is not None)
            wait_for("the Sniffer's window back", lambda: green() >= alone * 0.95)
            print("ok: with the desktop hidden in the background, the Sniffer's windows hide too")
        finally:
            for p in reversed(processes):
                p.terminate()
            for p in processes:
                try:
                    p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()

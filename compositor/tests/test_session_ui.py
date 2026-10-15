"""The session's own UI, with the real menu bar and Sniffer in a headless
zacos9-wm: ⌘-Tab and Option-click on the desktop (compositor/src/input.c,
view.c, the menu bar's app_switch_target); a contextual menu on the desktop
(the Sniffer asks, the menu bar opens it at the pointer); then Special > Log
Out (shell/menubar/logout.c); Option-choosing a program in the
Application menu and its ribbed handle (menus.c). Between them, General Controls' "Show Desktop
when in background", off: the desktop's icons go while a program is in
front, and a click there goes nowhere.

usage: test_session_ui.py ZACOS9-WM MENUBAR FINDER IDLE-CLIENT VPTR
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

            # Option-choosing Alpha in the Application menu (titled Bravo, at the
            # bar's right; its programs most recent first, so the second is the
            # other one): Bravo, the program left, hides too.
            vp("home", "move", "1250", "9", "hold", "option", "down", "wait", "300",
               "move", "-40", "88", "wait", "300", "up", "release", "option", "wait", "800")
            assert typed("z") == "alpha", "Option-choosing Alpha brings it forward"
            hidden = green() if have_grim else None
            if have_grim:
                assert hidden < before, "and hides Bravo, the program you were in"
            vp("home", "move", "1250", "9", "down", "wait", "300", "move", "-40", "88", "wait", "300",
               "up", "wait", "800")
            assert typed("0") == "bravo", "choosing Bravo without Option brings it back"
            vp("home", "move", "1250", "9", "down", "wait", "300", "move", "-40", "88", "wait", "300",
               "up", "wait", "800")
            assert typed("1") == "alpha", "without Option, Alpha comes forward"
            if have_grim:
                assert green() > hidden, "and Bravo stays shown (below Alpha)"
            print("ok: Option-choosing a program in the Application menu hides the one you leave")

            # The Application menu's ribbed handle, before its title: a click
            # shows an icon instead of the program's name, and back. (Titled
            # Alpha, the handle is about 1217; as an icon, about 1232.)
            conf = root / "config/zacos9/desktop.conf"

            def title_area():
                path = root / "title.ppm"
                subprocess.run(["grim", "-g", "1240,2 30x15", "-t", "ppm", str(path)], env=env,
                               check=True, timeout=10)
                return path.read_bytes()

            vp("home", "move", "600", "300", "wait", "200")  # the pointer out of the way
            named = title_area() if have_grim else None
            vp("home", "move", "1217", "9", "click", "move", "-400", "300", "wait", "500")
            assert "app-menu-name=false" in (conf.read_text() if conf.exists() else ""), \
                "a click on the handle hides the program's name"
            if have_grim:
                assert title_area() != named, "the title becomes an icon"
            vp("home", "move", "1232", "9", "click", "move", "-400", "300", "wait", "500")
            assert "app-menu-name" not in conf.read_text(), "another click shows the name again"
            if have_grim:
                assert title_area() == named, "and the title is the name again"
            print("ok: the Application menu's handle hides and shows the program's name")

            # Control-click on the desktop: its contextual menu opens at the
            # pointer and stays (a click); its first item, New Folder, makes one.
            desktop = root / "home" / "Desktop"
            desktop.mkdir(exist_ok=True)
            # (the windows cover the middle; the desktop shows at the left)
            vp("home", "move", "40", "450", "hold", "control", "click", "release", "control", "wait", "700")
            vp("home", "move", "70", "458", "click", "wait", "1500")
            wait_for("New Folder from the contextual menu", lambda: (desktop / "untitled folder").is_dir())
            print("ok: a contextual menu on the desktop opens at the pointer; New Folder works")

            # Show Desktop when in background, off (the Sniffer and zacos9-wm
            # read it as it changes): with Bravo in front, the startup disk's
            # icon (top right) goes, and a click on the desktop does nothing.
            if have_grim:
                def corner():
                    path = root / "corner.ppm"
                    subprocess.run(["grim", "-g", "1180,20 100x70", "-t", "ppm", str(path)],
                                   env=env, check=True, timeout=10)
                    return path.read_bytes()
                # (the Sniffer is in front, after New Folder)
                assert switch("z") == "alpha" and switch("c") == "bravo"
                shown = corner()
                conf = root / "config" / "zacos9" / "desktop.conf"
                text = conf.read_text() if conf.exists() else "[General]\n"
                conf.parent.mkdir(parents=True, exist_ok=True)
                # Written aside and renamed over, as the panels do (the Sniffer
                # watches the folder, which an in-place write doesn't touch).
                new = conf.with_suffix(".new")
                new.write_text(text.replace("[General]\n", "[General]\nshow-desktop=false\n", 1)
                               if "[General]\n" in text else "show-desktop=false\n" + text)
                new.replace(conf)
                wait_for("the desktop's icons to go", lambda: corner() != shown)
                vp("home", "move", "40", "450", "click", "wait", "400")
                assert typed("a") == "bravo", "a click on the hidden desktop leaves Bravo in front"
                assert switch("b") is None, "⌘-Tab brings the Sniffer back"
                wait_for("the desktop's icons back", lambda: corner() == shown)
                conf.write_text(text)
                print("ok: Show Desktop when in background, off, hides the desktop behind programs")

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

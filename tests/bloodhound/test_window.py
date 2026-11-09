"""Bloodhound's window (shell/bloodhound/): the Files channel searches a
folder and draws the matching files. The test starts a headless
zacos9-wm, opens Bloodhound on a temporary folder, types a query and checks
that the result rows add ink below the search controls.

usage: test_window.py ZACOS9-WM ZACOS9-BLOODHOUND VPTR
"""
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from zacos9test import wait_for

compositor, app, vptr = (str(Path(a).resolve()) for a in sys.argv[1:4])
if not shutil.which("grim"):
    print("SKIP: grim is not installed")
    sys.exit(77)

# The first window in a headless session: frame at (97, 32), content below
# the title bar. The region covers the window without the screen's edges.
WINDOW = (100, 50, 760, 620)


with tempfile.TemporaryDirectory(prefix="zacos9-bloodhound-") as directory:
    root = Path(directory)
    for name in ("home", "config", "data", "cache", "search"):
        (root / name).mkdir()
    search = root / "search"
    (search / "sub").mkdir()
    for name, text in [
        ("report.txt", "the harbour report\n"),
        ("notes.txt", "notes\n"),
        ("sub/report-two.txt", "the harbour again\n"),
    ]:
        (search / name).write_text(text)
    # The index answers a contents search; the saved query below asks it.
    fake_index = root / "tinysparql"
    fake_index.write_text("#!/bin/sh\necho 'Results:'\n"
                          f"echo '  -1, file://{search / 'report.txt'}'\n"
                          f"echo '  -2, file://{search / 'sub' / 'report-two.txt'}'\n")
    fake_index.chmod(0o755)
    config = root / "config" / "zacos9"
    config.mkdir(parents=True)
    (config / "find-searches.json").write_text(json.dumps({
        "version": 1,
        "searches": [{
            "name": "Harbour", "scope": 2, "folder": str(search),
            "criteria": [{"field": 1, "op": 17, "text": "harbour", "choice": 0}],
        }],
    }))
    actions = root / "actions"
    ctl = root / "zacos9-finder-ctl"
    ctl.write_text("#!/bin/sh\nprintf '%s\\n' \"$@\" >> " + shlex.quote(str(actions)) + "\n")
    ctl.chmod(0o755)
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, HOME=str(root / "home"),
               ZACOS9_FINDER_CTL=str(ctl), ZACOS9_TINYSPARQL=str(fake_index),
               XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
               XDG_CACHE_HOME=str(root / "cache"),
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
               ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="", ZACOS9_COLLAR="",
               ZACOS9_INHIBIT="", ZACOS9_DISK_HELPER="",
               QT_QPA_PLATFORM="wayland", QT_STYLE_OVERRIDE="",
               QT_WAYLAND_DISABLE_WINDOWDECORATION="1")
    env.pop("WAYLAND_DISPLAY", None)
    processes = []
    with (root / "log").open("w+") as log:
        try:
            server = subprocess.Popen([compositor], env=env, stdout=log, stderr=log)
            processes.append(server)
            sockets = wait_for("the compositor", lambda: server.poll() is None and [
                p for p in root.glob("wayland-*") if not p.name.endswith(".lock")])
            env["WAYLAND_DISPLAY"] = sockets[0].name
            time.sleep(1)

            window = subprocess.Popen([app, str(search)], env=env, stdout=log, stderr=log)
            processes.append(window)
            time.sleep(2)

            def vp(*args):
                subprocess.run([vptr, *args], env=env, check=True, timeout=10)

            def ink():
                path = root / "shot.ppm"
                x, y, w, h = WINDOW
                subprocess.run(["grim", "-g", f"{x},{y} {w}x{h}", "-t", "ppm", str(path)],
                               env=env, check=True, timeout=10)
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
                dark = 0
                for i in range(0, len(pixels) - 2, 3):
                    if pixels[i] < 80 and pixels[i + 1] < 80 and pixels[i + 2] < 80:
                        dark += 1
                return dark

            def highlight():
                path = root / "highlight.ppm"
                x, y, w, h = WINDOW
                subprocess.run(["grim", "-g", f"{x},{y} {w}x{h}", "-t", "ppm", str(path)],
                               env=env, check=True, timeout=10)
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
                marked = 0
                for i in range(0, len(pixels) - 2, 3):
                    if pixels[i] > 220 and pixels[i + 1] > 200 and pixels[i + 2] < 180:
                        marked += 1
                return marked

            # The first key of the session's first virtual keyboard goes on
            # its keymap. Then click the search field (window frame at 97,32)
            # and type: the default button searches on Return.
            vp("key", "Escape", "wait", "300")
            before = ink()
            vp("home", "move", "220", "95", "click", "wait", "300",
               "type", "report", "wait", "300", "key", "Return", "wait", "2000")
            after = ink()
            assert window.poll() is None, "Bloodhound exited"
            assert after > before + 500, (before, after)
            print("ok: Bloodhound searches a folder and draws the results")

            # Tab reaches the list, Down selects a result, and Return, ⌘R
            # and ⌘I ask the Sniffer to open, reveal and show Get Info.
            vp("key", "Tab", "wait", "200", "key", "Down", "wait", "200",
               "key", "Return", "wait", "500",
               "cmd", "r", "wait", "500", "cmd", "i", "wait", "500")
            asked = actions.read_text() if actions.exists() else ""
            lines = asked.splitlines()
            assert "open" in lines and "reveal" in lines and "command" in lines, asked
            assert ".txt" in asked and "report" in asked, asked
            print("ok: a result opens, reveals and shows Get Info through the Sniffer")

            # A saved contents search loads its criteria, asks the index and
            # draws snippets with the matched word highlighted.
            before_highlight = highlight()
            vp("home", "move", "431", "127", "click", "wait", "700",
               "home", "move", "430", "142", "click", "wait", "2500")
            after_highlight = highlight()
            assert window.poll() is None, "Bloodhound exited after a saved search"
            assert after_highlight > before_highlight + 50, (before_highlight, after_highlight)
            print("ok: a saved contents search highlights its matches in snippets")
        finally:
            for p in reversed(processes):
                if p.poll() is None:
                    p.terminate()
            for p in processes:
                try:
                    p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()

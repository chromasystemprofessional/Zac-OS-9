"""The Pack (shell/pack, Mac OS 9's Application palette): torn off the menu
bar's Application menu; a click on a tile brings that program forward,
Option-click hides the one you leave; it remembers being open; its close box
puts it away.

usage: test_pack.py ZACOS9-WM MENUBAR IDLE-CLIENT VPTR
"""
import os
import shlex
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

compositor, menubar, client, vptr = (str(Path(a).resolve()) for a in sys.argv[1:5])


def wait_for(what, check, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(0.1)
    raise AssertionError(f"timed out waiting for {what}")


with tempfile.TemporaryDirectory(prefix="zacos9-pack-") as directory:
    root = Path(directory)
    apps = root / "data" / "applications"
    apps.mkdir(parents=True)
    (root / "config" / "zacos9").mkdir(parents=True)
    for app_id, name in (("alpha", "Alpha"), ("bravo", "Bravo")):
        (apps / f"{app_id}.desktop").write_text(
            f"[Desktop Entry]\nType=Application\nName={name}\nExec=true\n")
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, HOME=directory,
               XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
               ZACOS9_STARTUP="0", ZACOS9_MENUBAR=shlex.quote(menubar), ZACOS9_FINDER="",
               ZACOS9_COLLAR="", ZACOS9_INHIBIT="", QT_QPA_PLATFORM="wayland", QT_STYLE_OVERRIDE="",
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
            time.sleep(2)  # the menu bar

            def vp(*args):
                subprocess.run([vptr, *args], env=env, check=True, timeout=20)

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
                vp("type", letter, "wait", "300")
                got = [a for a, k in keys.items() if f"key {letter}" in k]
                return got[0] if got else None

            window("alpha")
            window("bravo")
            pack_ini = root / "data" / "zacos9" / "pack.ini"
            conf = root / "config" / "zacos9" / "desktop.conf"

            # Drag the Application menu ("Bravo", at the bar's right) down
            # past its bottom and let go: the Pack appears there.
            vp("key", "Escape", "wait", "200", "home", "move", "1250", "9", "down", "wait", "300",
               "move", "-120", "150", "wait", "150", "move", "-130", "250", "wait", "200", "up")
            wait_for("the Pack", pack_ini.exists, 15)
            time.sleep(1)
            assert "pack-open=true" in conf.read_text(), "it will be open again next time"
            print("ok: the Application menu tears off into the Pack")

            # Tiles: Alpha, Bravo, Sniffer, 22 px each below a 13 px title
            # bar, the palette's top left where the pointer let go (less
            # 10, 6), the menu bar's 20 px above it.
            x0, y0 = 1000 - 10, 409 - 6 + 20
            tile = lambda i: (str(x0 + 40), str(y0 + 1 + 13 + 22 * i + 11))
            vp("home", "move", *tile(0), "click", "wait", "600")
            assert typed("a") == "alpha", "a click on Alpha's tile brings Alpha forward"
            print("ok: a click on a tile brings its program forward")

            vp("home", "move", *tile(1), "hold", "option", "click", "release", "option", "wait", "600")
            assert typed("b") == "bravo", "Option-click brings Bravo forward..."
            vp("home", "move", *tile(0), "click", "wait", "600")
            assert typed("c") == "alpha"
            vp("home", "move", *tile(2), "hold", "option", "click", "release", "option", "wait", "600")
            vp("home", "move", *tile(1), "click", "wait", "600")
            assert typed("d") == "bravo"
            vp("cmd", "Tab", "wait", "400")
            print("ok: Option-click hides the program you leave")

            # The close box puts it away, for good.
            vp("home", "move", str(x0 + 1 + 3 + 4), str(y0 + 1 + 6), "click", "wait", "800")
            wait_for("the Pack to close", lambda: "pack-open" not in conf.read_text(), 5)
            print("ok: the close box puts it away")
        finally:
            subprocess.run(["pkill", "-f", f"^{Path(menubar).parent}/zacos9-pack"], check=False)
            for p in reversed(processes):
                p.terminate()
            for p in processes:
                try:
                    p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()

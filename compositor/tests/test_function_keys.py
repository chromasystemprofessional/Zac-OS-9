"""Function keys that open things (compositor/src/fkeys.c, input.c): an
assigned F-key starts its application, or brings it forward if it's running;
unassigned F-keys stay the programs'; with "Use F1 through F12 as function
keys", ⌘ with the key opens its item instead.

usage: test_function_keys.py ZACOS9-WM IDLE-CLIENT VPTR
"""
import os
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

compositor, client, vptr = (str(Path(a).resolve()) for a in sys.argv[1:4])


def wait_for(what, check, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError(f"timed out waiting for {what}")


with tempfile.TemporaryDirectory(prefix="zacos9-fkeys-") as directory:
    root = Path(directory)
    apps = root / "data" / "applications"
    apps.mkdir(parents=True)
    (root / "zacos9").mkdir()
    started = root / "started"
    (apps / "starter.desktop").write_text(
        f"[Desktop Entry]\nType=Application\nName=Starter\nExec=touch {started}\n")
    # An application that's already running: an idle client with its app id.
    (apps / "alpha.desktop").write_text(
        f"[Desktop Entry]\nType=Application\nName=Alpha\nExec=touch {root / 'alpha-again'}\n")
    conf = root / "zacos9" / "desktop.conf"
    conf.write_text(f"[General]\nfkey-5={apps / 'starter.desktop'}\nfkey-6={apps / 'alpha.desktop'}\n")
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_CONFIG_HOME=directory,
               XDG_DATA_HOME=str(root / "data"),
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

            def window(app_id):
                keys = []
                p = subprocess.Popen([client, "--keys", "--app-id", app_id], env=env,
                                     stdout=subprocess.PIPE, stderr=log, text=True)
                processes.append(p)
                threading.Thread(target=lambda: [keys.append(l.rstrip("\n")) for l in p.stdout],
                                 daemon=True).start()
                wait_for(f"{app_id} shown", lambda: "shown" in keys)
                return keys

            alpha = window("alpha")
            bravo = window("bravo")

            vp("key", "F5", "wait", "300")
            wait_for("F5 to start its application", started.exists)
            assert not any(k == "key " for k in bravo), "an assigned key isn't the program's"
            print("ok: an assigned F-key starts its application")

            vp("key", "F6", "wait", "400", "type", "k", "wait", "300")
            assert "key k" in alpha and "key k" not in bravo, "F6 brought the running Alpha forward"
            assert not (root / "alpha-again").exists(), "rather than starting it again"
            print("ok: an F-key for a running application brings it forward")

            vp("key", "F7", "wait", "300")
            wait_for("an unassigned F-key to reach the program", lambda: "key " in alpha)
            print("ok: unassigned F-keys stay the programs'")

            started.unlink()
            conf.write_text(conf.read_text() + "fkeys-command=true\n")
            time.sleep(0.3)
            before = alpha.count("key ")
            vp("key", "F5", "wait", "400")
            assert not started.exists() and alpha.count("key ") == before + 1, \
                "with F-keys as function keys, F5 is the program's"
            vp("cmd", "F5", "wait", "400")
            wait_for("⌘-F5 to start its application", started.exists)
            print("ok: as function keys, ⌘ with the key opens its item")
        finally:
            for p in reversed(processes):
                p.terminate()
            for p in processes:
                try:
                    p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()

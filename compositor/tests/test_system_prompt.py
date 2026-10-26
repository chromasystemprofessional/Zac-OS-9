"""A keyring password prompt (compositor/src/view.c) keeps the keyboard until
it is answered: a new window from the program asking, or a click on that
program's window, doesn't take the keys away from it. When it closes, the
program's window gets them back.

usage: test_system_prompt.py ZACOS9-WM IDLE-CLIENT VPTR
"""
import os
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

compositor, client, vptr = (str(Path(a).resolve()) for a in sys.argv[1:4])
# Other programs' windows open at the same place on a 1280 x 720 screen with
# no menu bar; their title bars are on the row y = 42.
TITLE = (640, 42)


def wait_for(what, check, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError(f"timed out waiting for {what}")


with tempfile.TemporaryDirectory(prefix="zacos9-prompt-") as directory:
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

            def window(app_id):
                keys = []
                p = subprocess.Popen([client, "--keys", "--app-id", app_id], env=env,
                                     stdout=subprocess.PIPE, stderr=log, text=True)
                processes.append(p)
                threading.Thread(target=lambda: [keys.append(l.strip()) for l in p.stdout],
                                 daemon=True).start()
                wait_for(f"{app_id} shown", lambda: "shown" in keys)
                return p, keys

            app, app_keys = window("alpha")
            vp("home", "move", *map(str, TITLE), "click", "wait", "500")
            # The first key from the virtual keyboard goes on its keymap.
            vp("type", "a", "wait", "500")
            vp("type", "b")
            wait_for("the program to get the key", lambda: "key b" in app_keys)

            prompt, prompt_keys = window("gcr-prompter")
            vp("type", "p")
            wait_for("the prompt to get the key", lambda: "key p" in prompt_keys)
            assert "key p" not in app_keys
            print("ok: the prompt comes forward with the keyboard")

            app2, app2_keys = window("alpha")
            vp("type", "n")
            wait_for("the prompt to keep the key", lambda: "key n" in prompt_keys)
            assert "key n" not in app2_keys, "a new window took the keys from the prompt"
            print("ok: a new window doesn't take the keyboard from the prompt")

            vp("home", "move", *map(str, TITLE), "click", "wait", "500")
            vp("type", "c")
            wait_for("the prompt to keep the key", lambda: "key c" in prompt_keys)
            assert "key c" not in app_keys and "key c" not in app2_keys, \
                "a click on the program's window took the keys from the prompt"
            print("ok: a click elsewhere doesn't take the keyboard from the prompt")

            prompt.terminate()
            prompt.wait(timeout=10)
            vp("type", "z")
            wait_for("the program to get the keys back",
                     lambda: "key z" in app_keys or "key z" in app2_keys)
            print("ok: the program gets the keyboard back when the prompt closes")
        except BaseException:
            log.seek(0)
            sys.stderr.write(log.read())
            raise
        finally:
            for p in reversed(processes):
                if p.poll() is None:
                    p.terminate()
                    try:
                        p.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        p.kill()

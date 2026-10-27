#!/usr/bin/python3
"""X11 windows that turn override-redirect after they're made (Tk's menus)
or the other way round don't take the compositor down: it aborted in
wlr_xwayland_surface_restack() when one took the focus, logging the user
out (the Extension Icons tool's menus did it).

usage: test_override_redirect.py ZACOS9-WM OVERRIDE-REDIRECT-CLIENT
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

if not shutil.which("Xwayland"):
    print("SKIP: Xwayland is needed")
    sys.exit(77)

compositor, client = (Path(arg).resolve() for arg in sys.argv[1:3])

with tempfile.TemporaryDirectory(prefix="zacos9-override-redirect-") as directory:
    root = Path(directory)
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_CONFIG_HOME=directory,
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
               ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="",
               ZACOS9_COLLAR="", ZACOS9_INHIBIT="",
               OR_DISPLAY=str(root / "display"))
    env.pop("WAYLAND_DISPLAY", None)
    with (root / "server.log").open("w+") as log:
        server = subprocess.Popen([compositor, "-s", 'printf "%s" "$DISPLAY" > "$OR_DISPLAY"'],
                                  env=env, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline and not (root / "display").exists():
                assert server.poll() is None, "Compositor exited before starting Xwayland"
                time.sleep(0.02)
            assert (root / "display").exists(), "Compositor did not start Xwayland"
            env["DISPLAY"] = (root / "display").read_text()
            out = subprocess.run([client], env=env, capture_output=True, text=True, timeout=30)
            log.seek(0)
            server_log = log.read()
            assert "Assertion" not in server_log, server_log
            assert server.poll() is None, "Compositor died:\n" + server_log
            assert out.returncode == 0 and out.stdout.strip() == "ok", out.stderr
            print("ok: windows switching override-redirect are kept, both ways")
        finally:
            if server.poll() is None:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()

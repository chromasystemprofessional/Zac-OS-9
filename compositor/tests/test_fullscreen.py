#!/usr/bin/python3
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

compositor, client = (str(Path(arg).resolve()) for arg in sys.argv[1:3])
with tempfile.TemporaryDirectory(prefix="zacos9-fullscreen-") as directory:
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_CONFIG_HOME=directory,
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="2",
               ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="",
               ZACOS9_COLLAR="",
               QT_QPA_PLATFORM="wayland", QT_STYLE_OVERRIDE="", QT_WAYLAND_DISABLE_WINDOWDECORATION="1")
    env.pop("WAYLAND_DISPLAY", None)
    # The compositor provides its private Xwayland display to the X11 probe.
    x11_result = Path(directory) / "x11-result"
    env["FULLSCREEN_CLIENT"] = client
    env["FULLSCREEN_RESULT"] = str(x11_result)
    with (Path(directory) / "compositor.log").open("w+") as log:
        server = subprocess.Popen(
            [compositor, "-s", 'QT_QPA_PLATFORM=xcb "$FULLSCREEN_CLIENT" >"$FULLSCREEN_RESULT" 2>&1; '
             'echo $? >"$FULLSCREEN_RESULT.status"'], env=env, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 10
            while True:
                sockets = [p for p in Path(directory).glob("wayland-*") if not p.name.endswith(".lock")]
                if sockets:
                    break
                if server.poll() is not None or time.monotonic() > deadline:
                    log.seek(0)
                    raise RuntimeError("Compositor failed to start:\n" + log.read())
                time.sleep(0.02)
            env["WAYLAND_DISPLAY"] = sockets[0].name
            result = subprocess.run([client], env=env, text=True, capture_output=True, timeout=10)
            print("Wayland:", result.stdout, result.stderr)
            assert result.returncode == 0, "Wayland fullscreen failed"
            status = Path(str(x11_result) + ".status")
            while not status.exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            assert status.exists(), "Xwayland fullscreen probe did not finish"
            print("Xwayland:", x11_result.read_text())
            assert status.read_text().strip() == "0", "Xwayland fullscreen failed"
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()

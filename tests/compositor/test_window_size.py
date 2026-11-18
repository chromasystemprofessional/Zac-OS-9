#!/usr/bin/python3
"""A program's window opens at the size the program asks for (issue #184:
ZacOS 9 stretched every third-party main window to almost fill the screen,
overriding terminals' 80x24 and small utilities' own sizes, and making
dialogs open past the screen's edges).

The idle client resizes itself to 320 x 200 and paints its content green;
its window must still be that size in both directions, on Wayland and under
X11 (where the size is in the window and its size hints).

usage: test_window_size.py ZACOS9-WM IDLE-CLIENT
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("SKIP: Pillow is needed for screen comparisons")
    sys.exit(77)
if not shutil.which("grim") or not shutil.which("Xwayland"):
    print("SKIP: grim and Xwayland are needed for window size checks")
    sys.exit(77)

compositor, client = (Path(arg).resolve() for arg in sys.argv[1:3])
GREEN = (0, 0xAA, 0)
SIZE = (320, 200)
# The desktop strip around a new main window on the 1280 x 720 headless
# screen; the content starts inside the Platinum frame's 6 px borders.
PLACE = (96 + 6, 32 + 22)


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


with tempfile.TemporaryDirectory(prefix="zacos9-window-size-") as directory:
    root = Path(directory)
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_CONFIG_HOME=directory,
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
               ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="",
               ZACOS9_COLLAR="", ZACOS9_INHIBIT="",
               QT_STYLE_OVERRIDE="", QT_WAYLAND_DISABLE_WINDOWDECORATION="1",
               SIZE_DISPLAY=str(root / "display"))
    env.pop("WAYLAND_DISPLAY", None)
    with (root / "server.log").open("w+") as log:
        server = subprocess.Popen([compositor, "-s", 'printf "%s" "$DISPLAY" > "$SIZE_DISPLAY"'],
                                  env=env, stdout=log, stderr=log)
        probe = None
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                sockets = [p for p in root.glob("wayland-*") if not p.name.endswith(".lock")]
                if sockets and (root / "display").exists():
                    break
                assert server.poll() is None, "Compositor exited before creating its socket"
                time.sleep(0.02)
            assert sockets and (root / "display").exists(), "Compositor did not start"
            env["WAYLAND_DISPLAY"] = sockets[0].name
            env["DISPLAY"] = (root / "display").read_text()

            for platform in ("wayland", "xcb"):
                probe_env = dict(env, QT_QPA_PLATFORM=platform)
                with (root / f"{platform}.log").open("w") as errors:
                    probe = subprocess.Popen([client, "--keys"], env=probe_env,
                                             stdout=subprocess.PIPE, stderr=errors, text=True)
                    assert probe.stdout.readline().strip() == "shown"
                    time.sleep(0.5)
                    shot = root / f"{platform}.png"
                    subprocess.run(["grim", str(shot)], env=env, check=True,
                                   capture_output=True, timeout=5)
                    image = Image.open(shot).convert("RGB")
                    pixels = image.load()
                    xs, ys = [], []
                    for y in range(image.height):
                        for x in range(image.width):
                            if pixels[x, y] == GREEN:
                                xs.append(x)
                                ys.append(y)
                    assert xs, f"{platform}: the window did not show"
                    box = (min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1)
                    assert box[2:] == SIZE, (
                        f"{platform}: the window is {box[2:]} px, "
                        f"not the {SIZE} the program asked for")
                    assert box[:2] == PLACE, \
                        f"{platform}: the window opened at {box[:2]}, not inside the desktop strip"
                    probe.terminate()
                    probe.wait(timeout=5)
                    probe = None
                print(f"ok: {platform} window keeps the size its program chose")
        finally:
            if probe:
                stop(probe)
            stop(server)
            log.seek(0)
            errors = [line for line in log.read().splitlines() if "[ERROR]" in line]
            if errors:
                print("compositor errors:", *errors, sep="\n  ", file=sys.stderr)

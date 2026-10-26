#!/usr/bin/python3
"""A window hidden but not closed takes its title bar with it (issue #33:
Krita's splash screen left its frame behind). An X11 window is unmapped
and kept, which is the case that went wrong; Qt on Wayland destroys the
surface instead, so that platform is a sanity pass.

usage: test_ghost_frames.py ZACOS9-WM BUSY-CLIENT
"""
import os
import select
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

try:
    from PIL import Image, ImageChops
except ImportError:
    print("SKIP: Pillow is needed for screen comparisons")
    sys.exit(77)
if not shutil.which("grim") or not shutil.which("Xwayland"):
    print("SKIP: grim and Xwayland are needed for ghost frame checks")
    sys.exit(77)

compositor, client = (Path(arg).resolve() for arg in sys.argv[1:3])


def expect_line(process, expected):
    assert select.select([process.stdout], [], [], 5)[0], f"Timed out waiting for {expected}"
    assert process.stdout.readline().strip() == expected


def command(process, text):
    process.stdin.write(text + "\n")
    process.stdin.flush()
    expect_line(process, "ok")
    time.sleep(0.3)


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


with tempfile.TemporaryDirectory(prefix="zacos9-ghost-frames-") as directory:
    root = Path(directory)
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_CONFIG_HOME=directory,
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
               ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="",
               ZACOS9_COLLAR="", ZACOS9_INHIBIT="",
               QT_STYLE_OVERRIDE="", QT_WAYLAND_DISABLE_WINDOWDECORATION="1",
               GHOST_DISPLAY=str(root / "display"))
    env.pop("WAYLAND_DISPLAY", None)
    with (root / "server.log").open("w+") as log:
        server = subprocess.Popen([compositor, "-s", 'printf "%s" "$DISPLAY" > "$GHOST_DISPLAY"'],
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

            def shot(name):
                path = root / f"{name}.png"
                subprocess.run(["grim", str(path)], env=env, check=True,
                               capture_output=True, timeout=5)
                return Image.open(path).convert("RGB")

            def same(a, b):
                return ImageChops.difference(a, b).getbbox() is None

            for platform in ("xcb", "wayland"):
                probe_env = dict(env, QT_QPA_PLATFORM=platform)
                with (root / f"{platform}.log").open("w") as errors:
                    probe = subprocess.Popen([client], env=probe_env, stdin=subprocess.PIPE,
                                             stdout=subprocess.PIPE, stderr=errors, text=True)
                    expect_line(probe, "ready")
                    time.sleep(0.3)
                    empty = shot(f"{platform}-empty")
                    command(probe, "show")
                    time.sleep(0.5)
                    shown = shot(f"{platform}-shown")
                    assert not same(empty, shown), f"{platform}: the window never appeared"
                    command(probe, "hide")
                    time.sleep(0.5)
                    hidden = shot(f"{platform}-hidden")
                    if not same(empty, hidden):
                        box = ImageChops.difference(empty, hidden).getbbox()
                        raise AssertionError(f"{platform}: something of the window is left "
                                             f"on screen at {box} after it was hidden")
                    # And it comes back whole, in a frame, when shown again.
                    command(probe, "show")
                    time.sleep(0.5)
                    assert not same(empty, shot(f"{platform}-again")), f"{platform}: not shown again"
                    command(probe, "quit")
                    probe.wait(timeout=5)
                    probe = None
                print(f"ok: {platform} window hidden takes its frame with it")
        finally:
            if probe:
                stop(probe)
            stop(server)
            log.seek(0)
            errors = [line for line in log.read().splitlines() if "[ERROR]" in line]
            if errors:
                print("compositor errors:", *errors, sep="\n  ", file=sys.stderr)

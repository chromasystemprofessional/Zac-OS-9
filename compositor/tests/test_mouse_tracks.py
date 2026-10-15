#!/usr/bin/env python3
"""The Mouse panel's options, drawn by zacos9-wm. Mouse Tracks
(compositor/src/tracks.c): with it on, a trail of arrows follows the
moving pointer and goes once it rests; off, there is none. Thick I-Beam:
over Scribble's text (Qt asks for the text cursor by cursor-shape-v1),
the I-beam has more ink with it on.

usage: test_mouse_tracks.py ZACOS9-WM VPTR SCRIBBLE
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

compositor, vptr, scribble = (str(Path(a).resolve()) for a in sys.argv[1:4])
if not shutil.which("grim"):
    print("SKIP: grim is not installed")
    sys.exit(77)
AREA = (250, 250, 600, 350)  # where the pointer moves, on a 1280x720 screen


def wait_for(what, check, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError(f"timed out waiting for {what}")


def black_after_moving(tracks):
    """Black pixels in AREA just after a move to the right, and a second later."""
    return session("mouse-tracks=true\n" if tracks else "", moving_pointer)


def ibeam_ink(thick):
    """Black pixels of the pointer over Scribble's text."""
    return session("thick-ibeam=true\n" if thick else "", over_text)


def moving_pointer(env, black):
    subprocess.run([vptr, "home", "move", "300", "300", "wait", "300"], env=env, check=True)
    subprocess.run([vptr, "move", "200", "0"], env=env, check=True)
    moving = black()
    time.sleep(1)
    return moving, black()


def over_text(env, black):
    # A keyboard first: until one appears the seat offers programs no pointer.
    subprocess.run([vptr, "key", "Escape"], env=env, check=True)
    editor = subprocess.Popen([scribble], env=env, stderr=subprocess.DEVNULL)
    try:
        time.sleep(3)
        subprocess.run([vptr, "home", "move", "400", "300", "wait", "300", "move", "5", "5",
                        "wait", "500"], env=env, check=True)
        return black(cursor=True, area=(395, 290, 425, 320))
    finally:
        editor.terminate()
        editor.wait(5)


def session(conf, act):
    with tempfile.TemporaryDirectory(prefix="zacos9-tracks-") as directory:
        root = Path(directory)
        (root / "zacos9").mkdir()
        (root / "zacos9" / "desktop.conf").write_text(conf)
        env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_CONFIG_HOME=directory,
                   WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
                   ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="", ZACOS9_COLLAR="",
                   ZACOS9_INHIBIT="")
        env.pop("WAYLAND_DISPLAY", None)
        with (root / "compositor.log").open("w+") as log:
            server = subprocess.Popen([compositor], env=env, stdout=log, stderr=log)
            try:
                sockets = wait_for("the compositor", lambda: server.poll() is None and [
                    p for p in root.glob("wayland-*") if not p.name.endswith(".lock")])
                env["WAYLAND_DISPLAY"] = sockets[0].name

                def black(cursor=False, area=AREA):
                    path = root / "shot.ppm"
                    x0, y0, x1, y1 = area
                    subprocess.run(["grim", *(["-c"] if cursor else []),
                                    "-g", f"{x0},{y0} {x1 - x0}x{y1 - y0}", "-t", "ppm",
                                    str(path)], env=env, check=True, timeout=10)
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
                    return sum(pixels[i:i + 3] == b"\0\0\0" for i in range(0, len(pixels) - 2, 3))

                return act(env, black)
            finally:
                server.terminate()
                server.wait(5)


moving, resting = black_after_moving(True)
assert moving > 3 * resting > 0, (moving, resting)
print("ok: with Mouse Tracks on, a trail follows the moving pointer and goes once it rests")
moving, resting = black_after_moving(False)
assert moving <= resting * 1.5, (moving, resting)
print("ok: with it off, there is no trail")
thin, thick = ibeam_ink(False), ibeam_ink(True)
assert thin > 0 and thick > thin * 1.3, (thin, thick)
print("ok: Thick I-Beam: a bolder text pointer over a Qt program's text")

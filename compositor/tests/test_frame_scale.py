#!/usr/bin/env python3
"""Window frames at 1.25x, 1.5x and 1.75x (compositor/src/frame.c, decor.c,
lib/draw.c pl_snap): drawn at the screen's own pixels, the active title
bar's stripes are six even ridges, every white line as thick as every
other and every gray one too, and the same wherever the window is: also
after it is dragged a few pixels (the frame's pixels fall differently
there). At 2x,
as before, two pixels each.

usage: test_frame_scale.py ZACOS9-WM IDLE-CLIENT VPTR RAWSHOT
"""
import os
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

compositor, client, vptr, rawshot = (str(Path(a).resolve()) for a in sys.argv[1:5])
WHITE, GRAY = (0xFF,) * 3, (0x77,) * 3


def wait_for(what, check, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError(f"timed out waiting for {what}")


failures = []


def check(ok, what):
    print(("ok: " if ok else "FAIL: ") + what)
    if not ok:
        failures.append(what)


def ridges(column):
    """The stripes in one column of pixels: [(white, gray) thickness, ...]
    for the longest run of white-then-gray pairs, or []."""
    runs = []
    for rgb in column:
        if runs and runs[-1][0] == rgb:
            runs[-1][1] += 1
        else:
            runs.append([rgb, 1])
    best, i = [], 0
    while i < len(runs):
        pairs = []
        while i + 1 < len(runs) and runs[i][0] == WHITE and runs[i + 1][0] == GRAY:
            pairs.append((runs[i][1], runs[i + 1][1]))
            i += 2
        best = max(best, pairs, key=len)
        i += 1
    return best


def session(scale):
    with tempfile.TemporaryDirectory(prefix="zacos9-framescale-") as directory:
        root = Path(directory)
        (root / "zacos9").mkdir()
        (root / "zacos9" / "desktop.conf").write_text(f"scale={scale}\n")
        env = dict(os.environ, HOME=directory, XDG_RUNTIME_DIR=directory, XDG_CONFIG_HOME=directory,
                   WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
                   ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="", ZACOS9_COLLAR="",
                   ZACOS9_INHIBIT="", QT_QPA_PLATFORM="wayland", QT_STYLE_OVERRIDE="",
                   QT_WAYLAND_DISABLE_WINDOWDECORATION="1")
        env.pop("WAYLAND_DISPLAY", None)
        env.pop("ZACOS9_SCALE", None)
        processes = []
        with (root / "compositor.log").open("w+") as log:
            try:
                server = subprocess.Popen([compositor], env=env, stdout=log, stderr=log)
                processes.append(server)
                sockets = wait_for("the compositor", lambda: server.poll() is None and [
                    p for p in root.glob("wayland-*") if not p.name.endswith(".lock")])
                env["WAYLAND_DISPLAY"] = sockets[0].name

                def shot():
                    path = root / "shot.ppm"
                    subprocess.run([rawshot, "HEADLESS-1", str(path)], env=env, check=True, timeout=10)
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
                    return fields[0], fields[1], data[at + 1:]

                def stripes():
                    """{x: ridges} for every column of the screen that has six."""
                    w, h, px = shot()
                    found = {}
                    for x in range(0, w, 3):
                        column = [tuple(px[(y * w + x) * 3:(y * w + x) * 3 + 3]) for y in range(h // 3)]
                        r = ridges(column)
                        if len(r) == 6:
                            found[x] = (r, next(y for y in range(h // 3) if column[y] == WHITE))
                    return found

                keys = []
                p = subprocess.Popen([client, "--keys", "--app-id", "alpha"], env=env,
                                     stdout=subprocess.PIPE, stderr=log, text=True)
                processes.append(p)
                threading.Thread(target=lambda: [keys.append(l.strip()) for l in p.stdout],
                                 daemon=True).start()
                wait_for("the window", lambda: "shown" in keys)
                time.sleep(0.5)
                results = []
                for dx, dy in ((0, 0), (3, 1), (2, 3), (1, 2)):
                    if dx or dy:
                        x, y = results[-1][0]
                        subprocess.run([vptr, "home", "move", str(x), str(y), "down", "move", str(dx),
                                        str(dy), "up", "wait", "300"], env=env, check=True, timeout=10)
                    found = wait_for("the stripes", stripes, 5)
                    x, (r, top) = sorted(found.items())[len(found) // 4]
                    # A point on the stripes, in logical pixels, to drag by.
                    results.append(((round(x / scale), round((top + 3) / scale)), found))
                return [f for _, f in results]
            except Exception:
                log.seek(0)
                print(log.read()[-3000:], file=sys.stderr)
                raise
            finally:
                for process in reversed(processes):
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()


for scale, want in (("1.25", None), ("1.5", None), ("1.75", None), ("2", [(2, 2)] * 6)):
    places = session(float(scale))
    everywhere = {tuple(r) for found in places for r, _ in found.values()}
    check(len(everywhere) == 1, f"at {scale}x, the same ridges wherever the window is {sorted(everywhere)}")
    for n, found in enumerate(places):
        patterns = {tuple(r) for r, _ in found.values()}
        even = all(len(set(p)) == 1 for p in patterns)
        where = "where it opened" if n == 0 else f"after drag {n}"
        check(len(found) > 20 and len(patterns) == 1 and even and (want is None or
                                                                    [list(patterns)[0]] == [tuple(want)]),
              f"at {scale}x, {where}: six even ridges in {len(found)} columns {sorted(patterns)}")

sys.exit(1 if failures else 0)

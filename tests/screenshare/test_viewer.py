#!/usr/bin/env python3
"""zacos9-screenshare-viewer against a real wayvnc (tests the viewer, not
the host: wayvnc runs here without a password, which it allows only on a
private test address).

Two headless zacos9-wm sessions with screens of the same 1280 x 720
pixels: the host at 1x, with a green window; the guest at 1.5x (its
programs' 1280 x 720 pixels are 853 x 480 logical), running the viewer:

- full screen, the guest's screen shows the host's pixel for pixel,
  whatever the guest's Pixel size (no blur, no stretch), but for where
  each one's pointer is;
- a click on the guest lands on the same pixel of the host's window;
- a key typed on the guest reaches the host's window.

usage: test_viewer.py ZACOS9-WM VIEWER IDLE-CLIENT VPTR RAWSHOT
"""
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

compositor, viewer, client, vptr, rawshot = (str(Path(a).resolve()) for a in sys.argv[1:6])
if not shutil.which("wayvnc"):
    print("SKIP: wayvnc is not installed")
    sys.exit(77)
GREEN = bytes((0x00, 0xAA, 0x00))


def wait_for(what, check, seconds=15):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(0.1)
    raise AssertionError(f"timed out waiting for {what}")


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


failures = []


def check(ok, what):
    print(("ok: " if ok else "FAIL: ") + what)
    if not ok:
        failures.append(what)


processes = []
logs = []


def session(root, scale):
    (root / "zacos9").mkdir(parents=True)
    (root / "zacos9" / "desktop.conf").write_text(f"scale={scale}\n")
    env = dict(os.environ, HOME=str(root), XDG_RUNTIME_DIR=str(root), XDG_CONFIG_HOME=str(root),
               XDG_DATA_HOME=str(root), XDG_CACHE_HOME=str(root),
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
               ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="", ZACOS9_COLLAR="",
               ZACOS9_INHIBIT="", QT_QPA_PLATFORM="wayland", QT_STYLE_OVERRIDE="",
               QT_WAYLAND_DISABLE_WINDOWDECORATION="1")
    for key in ("WAYLAND_DISPLAY", "ZACOS9_SCALE", "DISPLAY"):
        env.pop(key, None)
    log = (root / "log").open("w+")
    logs.append(log)
    processes.append(subprocess.Popen([compositor], env=env, stdout=log, stderr=log))
    sockets = wait_for("a compositor", lambda: [p for p in root.glob("wayland-*")
                                                 if not p.name.endswith(".lock")])
    env["WAYLAND_DISPLAY"] = sockets[0].name
    return env, log


def shot(env, root):
    path = root / "shot.ppm"
    wait_for("a picture", lambda: subprocess.run([rawshot, "HEADLESS-1", str(path)], env=env,
                                                 stderr=subprocess.DEVNULL).returncode == 0, 5)
    data = path.read_bytes()
    header = data.split(b"\n", 3)
    w, h = map(int, header[1].split())
    return w, h, header[3]


def at(px, w, x, y):
    return px[(y * w + x) * 3:(y * w + x) * 3 + 3]


with tempfile.TemporaryDirectory(prefix="zacos9-viewer-") as directory:
    top = Path(directory)
    try:
        host_env, host_log = session(top / "host", 1)
        guest_env, guest_log = session(top / "guest", 1.5)

        keys = []
        window = subprocess.Popen([client, "--keys"], env=host_env, stdout=subprocess.PIPE,
                                  stderr=host_log, text=True)
        processes.append(window)
        threading.Thread(target=lambda: [keys.append(l.strip()) for l in window.stdout],
                         daemon=True).start()
        wait_for("the host's window", lambda: "shown" in keys)

        port = free_port()
        processes.append(subprocess.Popen(["wayvnc", "-o", "HEADLESS-1", "127.0.0.1", str(port)],
                                          env=host_env, stdout=host_log, stderr=host_log))

        def listening():
            with socket.socket() as s:
                return s.connect_ex(("127.0.0.1", port)) == 0
        wait_for("wayvnc", listening)

        processes.append(subprocess.Popen([viewer, f"127.0.0.1:{port}", "Host"], env=guest_env,
                                          stdout=guest_log, stderr=guest_log))

        # The host's pointer and the guest's are each drawn where they are.
        hw, hh, host = shot(host_env, top / "host")

        def pointer(x, y):
            return 90 <= x < 130 and 90 <= y < 130 or 140 <= x < 200 and 140 <= y < 200

        def matches():
            gw, gh, guest = shot(guest_env, top / "guest")
            if (gw, gh) != (hw, hh):
                return False
            wrong = sum(1 for y in range(0, hh, 2) for x in range(0, hw, 2)
                        if not pointer(x, y) and at(guest, gw, x, y) != at(host, hw, x, y))
            return wrong == 0
        try:
            wait_for("the host's screen on the guest", matches, 20)
            check(True, "at 1.5x, the guest shows the host's 1280 x 720 pixels one for one")
        except AssertionError:
            gw, gh, guest = shot(guest_env, top / "guest")
            wrong = sum(1 for y in range(0, hh, 2) for x in range(0, hw, 2)
                        if not pointer(x, y) and at(guest, gw, x, y) != at(host, hw, x, y))
            check(False, f"at 1.5x, the guest shows the host's pixels one for one "
                         f"({gw}x{gh}; {wrong} of {hw * hh // 4} sampled pixels differ)")

        # A click: the middle of the host's green window, through the guest.
        greens = [(x, y) for y in range(0, hh, 4) for x in range(0, hw, 4) if at(host, hw, x, y) == GREEN]
        gx = (min(x for x, _ in greens) + max(x for x, _ in greens)) // 2
        gy = (min(y for _, y in greens) + max(y for _, y in greens)) // 2
        left = min(x for x, _ in greens)
        topy = min(y for _, y in greens)
        # The guest's pointer moves in its logical pixels: 1.5 of its screen's.
        subprocess.run([vptr, "home", "move", str(round(gx / 1.5)), str(round(gy / 1.5)), "click",
                        "wait", "300"], env=guest_env, check=True, timeout=20)
        press = wait_for("the click on the host", lambda: next((k for k in keys if k.startswith("press")), None))
        px, py = map(int, press.split()[1:3])
        check(abs((left + px) - gx) <= 2 and abs((topy + py) - gy) <= 2,
              f"a click on the guest lands where it shows on the host ({left + px}, {topy + py} "
              f"for {gx}, {gy})")

        # The guest's first keyboard (vptr's) and the host's (wayvnc's)
        # come with the first key: typed again until it lands. The host
        # has no keyboard of its own, as a screen-sharing host may not.
        def typed():
            subprocess.run([vptr, "wait", "300", "type", "k", "wait", "300"], env=guest_env, check=True,
                           timeout=20)
            return "key k" in keys
        wait_for("the key on the host", typed, 20)
        check(True, "a key typed on the guest reaches the host's window")
    except Exception:
        for log in logs:
            log.seek(0)
            text = log.read()
            print(text[-2500:], file=sys.stderr)
        raise
    finally:
        for p in reversed(processes):
            p.terminate()
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()

sys.exit(1 if failures else 0)

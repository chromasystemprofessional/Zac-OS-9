#!/usr/bin/env python3
"""The lock at 1.5x and 1.75x (shell/lock/lock.c, wp_fractional_scale_v1,
lib/draw.c pl_snap): drawn at the screen's own pixels, it is as sharp as
at 1x, made only of the colours it has there (but for the pointer, which
zacos9-wm draws). Drawn at 2x and shrunk, its
edges and text would blur into colours of their own.

usage: test_lock_scale.py ZACOS9-WM LOCK VPTR RAWSHOT
"""
import os
import shlex
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from zacos9test import wait_for

compositor, lock, vptr, rawshot = (str(Path(a).resolve()) for a in sys.argv[1:5])
MINUTE = 400


def colours(scale):
    """The set of colours on the locked screen, its lock showing."""
    with tempfile.TemporaryDirectory(prefix="zacos9-lockscale-") as directory:
        root = Path(directory)
        (root / "zacos9").mkdir()
        (root / "zacos9" / "desktop.conf").write_text(
            f"[General]\nscale={scale}\nscreensaver=blank\nscreensaver-after=1\ndisplay-sleep=60\n"
            "require-password=1\n")
        env = dict(os.environ, HOME=directory, XDG_RUNTIME_DIR=directory, XDG_CONFIG_HOME=directory,
                   WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
                   ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="", ZACOS9_COLLAR="",
                   ZACOS9_LOCK=shlex.quote(lock), ZACOS9_INHIBIT="", ZACOS9_SUSPEND_COMMAND="",
                   ZACOS9_IDLE_MINUTE_MS=str(MINUTE), DBUS_SYSTEM_BUS_ADDRESS="unix:path=/nonexistent")
        env.pop("WAYLAND_DISPLAY", None)
        env.pop("ZACOS9_SCALE", None)
        with (root / "compositor.log").open("w+") as log:
            server = subprocess.Popen([compositor], env=env, stdout=log, stderr=log)
            try:
                sockets = wait_for("the compositor", lambda: server.poll() is None and [
                    p for p in root.glob("wayland-*") if not p.name.endswith(".lock")])
                env["WAYLAND_DISPLAY"] = sockets[0].name
                state = root / f"zacos9-idle-{sockets[0].name}"
                wait_for("the lock", lambda: state.exists() and "locked 1" in state.read_text(), 10)
                # No screen saver back over it for a while ("minutes" are 400 ms).
                (root / "zacos9" / "desktop.conf").write_text(
                    f"[General]\nscale={scale}\nscreensaver=blank\nscreensaver-after=60\n"
                    "display-sleep=90\nrequire-password=1\n")
                # Woken: the lock shows; the pointer off its dialog.
                subprocess.run([vptr, "home", "move", "2", "2"], env=env, check=True, timeout=10)
                wait_for("the screen awake", lambda: "HEADLESS-1 awake" in state.read_text(), 5)
                time.sleep(1.0)
                path = root / "shot.ppm"
                # (Just woken, the screen may not have a frame to read yet.)
                wait_for("a picture of the screen", lambda: subprocess.run(
                    [rawshot, "HEADLESS-1", str(path)], env=env, timeout=10,
                    stderr=subprocess.DEVNULL).returncode == 0, 5)
                data = path.read_bytes()
                header = data.split(b"\n", 3)
                w = int(header[1].split()[0])
                pixels = header[3]
                # Not the pointer's corner: zacos9-wm draws it from the
                # cursor theme at the screen's scale.
                return {pixels[i:i + 3] for i in range(0, len(pixels) - 2, 3)
                        if (i // 3) % w >= 80 or (i // 3) // w >= 80}
            except Exception:
                log.seek(0)
                print(log.read()[-3000:], file=sys.stderr)
                raise
            finally:
                server.terminate()
                try:
                    server.wait(5)
                except subprocess.TimeoutExpired:
                    server.kill()


failures = []
at1 = colours(1)
if len(at1) < 4:
    print(f"FAIL: the lock didn't show at 1x ({len(at1)} colours)")
    sys.exit(1)
for scale in (1.5, 1.75):
    at = colours(scale)
    extra = at - at1
    ok = not extra and len(at) >= len(at1) // 2
    print(("ok: " if ok else "FAIL: ") + f"at {scale}x the lock has only its 1x colours "
          f"({len(at)} of {len(at1)}, {len(extra)} new)")
    if not ok:
        failures.append(scale)
sys.exit(1 if failures else 0)

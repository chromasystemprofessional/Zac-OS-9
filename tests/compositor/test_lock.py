#!/usr/bin/python3
"""Locking the screens ("Ask for my password to wake": compositor/src/lock.c,
shell/lock/lock.c built as test-lock, which takes "sesame"), on two
headless screens with 400 ms "minutes", under dbus-run-session:

- once every screen shows the screen saver, the session locks; the saver
  still plays above the lock;
- woken, the lock shows and nothing of the session does (screenshots: a
  green window is nowhere), and keys typed reach only the lock;
- a wrong password keeps it locked; if the lock program dies, the screens
  stay locked and it comes back, and another program can't take its place
  meanwhile (it would unlock at will); the right password unlocks, the window
  shows again and has the keyboard back;
- without the setting nothing locks; ScreenSaver.Lock locks regardless.
"""
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

from gi.repository import Gio, GLib

from zacos9test import wait_for

compositor, client, lock, helper, vptr = (str(Path(arg).resolve()) for arg in sys.argv[1:6])
MINUTE = 400
GREEN = (0x00, 0xAA, 0x00)


with tempfile.TemporaryDirectory(prefix="zacos9-lock-") as directory:
    root = Path(directory)
    (root / "zacos9").mkdir()
    conf = root / "zacos9" / "desktop.conf"
    # Nothing idles until the window is up; then the short timings below.
    conf.write_text("[General]\nscreensaver=blank\nscreensaver-after=60\ndisplay-sleep=90\n"
                    "require-password=1\n")
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_CONFIG_HOME=directory,
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="2",
               ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="", ZACOS9_COLLAR="",
               ZACOS9_LOCK=shlex.quote(lock), ZACOS9_INHIBIT=shlex.quote(helper), ZACOS9_IDLE_MINUTE_MS=str(MINUTE),
               ZACOS9_SUSPEND_COMMAND="",
               DBUS_SYSTEM_BUS_ADDRESS="unix:path=/nonexistent",
               QT_QPA_PLATFORM="wayland", QT_STYLE_OVERRIDE="",
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
            state_file = root / f"zacos9-idle-{sockets[0].name}"

            def state():
                try:
                    lines = state_file.read_text().split("\n")
                except FileNotFoundError:
                    return {}
                return dict(line.split()[-2:] if line.startswith("output") else line.split()
                            for line in lines if line.strip())

            def locked():
                return state().get("locked") == "1"

            def screens(value):
                s = state()
                return [s.get(name) for name in ("HEADLESS-1", "HEADLESS-2")] == [value, value]

            def move():
                subprocess.run([vptr, "move", "3", "3"], env=env, check=True, timeout=10)

            def type_text(text):
                subprocess.run([vptr, "type", text, "key", "Return"], env=env, check=True, timeout=10)

            def shot(output):
                """(green pixels, lit pixels) on a screen, sampled."""
                path = root / f"{output}.ppm"
                subprocess.run(["grim", "-t", "ppm", "-o", output, str(path)], env=env,
                               check=True, timeout=10)
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
                green = lit = 0
                for i in range(0, len(pixels) - 2, 3 * 31):
                    rgb = tuple(pixels[i:i + 3])
                    green += rgb == GREEN
                    lit += rgb != (0, 0, 0)
                return green, lit

            def green_anywhere():
                return any(shot(o)[0] for o in ("HEADLESS-1", "HEADLESS-2"))

            def lock_pids():
                """test-lock processes started by our compositor (through a shell)."""
                procs = {}
                for stat in Path("/proc").glob("[0-9]*/stat"):
                    try:
                        text = stat.read_text()
                    except OSError:
                        continue
                    fields = text[text.rindex(")") + 2:].split()
                    procs[int(stat.parent.name)] = (text[text.index("(") + 1:text.rindex(")")],
                                                    fields[0], int(fields[1]))
                pids = []
                for pid, (comm, state, parent) in procs.items():
                    grandparent = procs.get(parent, ("", "", 0))[2]
                    if comm == "test-lock" and state != "Z" and server.pid in (parent, grandparent):
                        pids.append(pid)
                return pids

            have_grim = shutil.which("grim") is not None

            window = subprocess.Popen([client, "--keys"], env=env, stdout=subprocess.PIPE,
                                      stderr=log, text=True)
            processes.append(window)
            keys = []
            threading.Thread(target=lambda: [keys.append(l.strip()) for l in window.stdout],
                             daemon=True).start()
            wait_for("the window", lambda: "shown" in keys)
            if have_grim:
                wait_for("the window on screen", green_anywhere)
            assert not locked()
            conf.write_text("[General]\nscreensaver=blank\nscreensaver-after=1\ndisplay-sleep=30\n"
                            "require-password=1\n")
            move()

            # Every screen in the screen saver: locked, the saver above the lock.
            wait_for("the screen saver on both", lambda: screens("saver"))
            wait_for("the lock", locked, 4)
            if have_grim:
                assert not green_anywhere(), "the session showed while locked"
                wait_for("the screen saver playing over the lock",
                         lambda: all(shot(o)[1] == 0 for o in ("HEADLESS-1", "HEADLESS-2")), 2)
            print("ok: once every screen shows the screen saver, the screens lock beneath it")

            move()
            wait_for("the screens awake", lambda: screens("awake"), 2)
            if have_grim:
                wait_for("the lock showing", lambda: any(shot(o)[1] for o in ("HEADLESS-1", "HEADLESS-2")), 2)
                assert not green_anywhere(), "a window showed through the lock"
            print("ok: woken, the lock shows, and no window does")

            def lock_said(word):
                """test-lock's own report (its stdout is the compositor's log)."""
                return (root / "compositor.log").read_text().split("\n").count(word)

            before = len([k for k in keys if k.startswith("key")])
            type_text("wrong")
            wait_for("the lock to reject it", lambda: lock_said("wrong") == 1, 3)
            assert locked(), "a wrong password unlocked"
            assert len([k for k in keys if k.startswith("key")]) == before, \
                f"keys typed at the lock reached a window: {keys}"
            print("ok: a wrong password keeps it locked, and what is typed reaches no window")

            pids = wait_for("the lock program", lock_pids)
            os.kill(pids[0], 9)
            time.sleep(0.3)
            assert locked(), "killing the lock program unlocked"
            # Before it comes back, a lock program the compositor didn't start.
            impostor = subprocess.Popen([lock], env=env, stdout=log, stderr=log)
            processes.append(impostor)
            wait_for("the impostor refused", lambda: "lock: refused a lock from process %d" % impostor.pid
                     in (root / "compositor.log").read_text(), 3)
            impostor.kill()
            impostor.wait()
            assert locked(), "another lock program's lock unlocked"
            if have_grim:
                assert not green_anywhere(), "a window showed once the lock program died"
            wait_for("the lock program back", lambda: lock_pids() and lock_pids() != pids, 4)
            print("ok: if the lock program dies, the screens stay locked and it comes back; "
                  "no other program may lock in its place")

            time.sleep(0.5)  # its surfaces up
            # No screen saver for a while: at 1 "minute" (400 ms) it came back
            # over the window before the screenshots below could see it.
            conf.write_text("[General]\nscreensaver=blank\nscreensaver-after=60\ndisplay-sleep=90\n"
                            "require-password=1\n")
            move()
            type_text("sesame")
            wait_for("unlocked", lambda: not locked(), 4)
            # Its own report can reach the log just after the compositor's state.
            wait_for("the lock to say it unlocked", lambda: lock_said("unlocked") == 1, 2)
            if have_grim:
                wait_for("the window back", green_anywhere, 2)
            subprocess.run([vptr, "type", "x"], env=env, check=True, timeout=10)
            wait_for("the window to have the keyboard again", lambda: "key x" in keys, 3)
            print("ok: the right password unlocks; the window shows and has the keyboard again")

            # Without the setting, the screen saver comes and goes unlocked.
            conf.write_text("[General]\nscreensaver=blank\nscreensaver-after=1\ndisplay-sleep=30\n")
            move()
            wait_for("the screen saver on both", lambda: screens("saver"))
            time.sleep(1)
            assert not locked(), "locked without the setting"
            move()
            print("ok: without \"Ask for my password\", nothing locks")

            # An app (or loginctl, through logind) asking: locked regardless.
            bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
            wait_for("zacos9-inhibit", lambda: bus.call_sync(
                "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner",
                GLib.Variant("(s)", ("org.freedesktop.ScreenSaver",)), GLib.VariantType("(b)"),
                Gio.DBusCallFlags.NONE, 1000, None).unpack()[0])
            bus.call_sync("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
                          "org.freedesktop.ScreenSaver", "Lock", None, None, Gio.DBusCallFlags.NONE, 5000, None)
            assert locked(), "Lock returned before the screens were locked"
            time.sleep(0.5)
            move()
            type_text("sesame")
            wait_for("unlocked", lambda: not locked(), 4)
            print("ok: ScreenSaver.Lock locks at once, the setting or not, and answers once locked")
        except Exception:
            log.seek(0)
            print(log.read()[-6000:], file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()

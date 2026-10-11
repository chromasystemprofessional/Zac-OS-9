#!/usr/bin/python3
"""Apps asking over D-Bus to stay awake (shell/inhibit/inhibit.c, through
zacos9-wm's keep_awake): org.freedesktop.ScreenSaver from a process with no
window keeps every screen and the system awake until UnInhibit; from an
app with a window, only that window's screen (and the system), until the
app leaves the bus; the portal's Inhibit until its Request is closed;
SimulateUserActivity wakes the screens and GetActive reports the screen
saver. Two headless screens, "minutes" of 400 ms; run in dbus-run-session."""
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from gi.repository import Gio, GLib

compositor, client, helper = (str(Path(arg).resolve()) for arg in sys.argv[1:4])
MINUTE = 400
SAVER = "org.freedesktop.ScreenSaver"


def wait_for(what, check, seconds=5):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(0.02)
    raise AssertionError(f"timed out waiting for {what}")


bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)


def call(name, path, iface, method, args=None, reply=None):
    result = bus.call_sync(name, path, iface, method, args,
                           GLib.VariantType(reply) if reply else None,
                           Gio.DBusCallFlags.NONE, 5000, None)
    return result.unpack() if result else None


def saver(method, args=None, reply=None):
    return call(SAVER, "/org/freedesktop/ScreenSaver", SAVER, method, args, reply)


def has_owner(name):
    return call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                "NameHasOwner", GLib.Variant("(s)", (name,)), "(b)")[0]


with tempfile.TemporaryDirectory(prefix="zacos9-inhibit-") as directory:
    root = Path(directory)
    (root / "zacos9").mkdir()
    (root / "zacos9" / "desktop.conf").write_text(
        "[General]\nscreensaver-after=1\ndisplay-sleep=2\nsystem-sleep=4\n")
    slept = root / "slept"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_CONFIG_HOME=directory,
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="2",
               ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="", ZACOS9_COLLAR="",
               ZACOS9_INHIBIT=helper, ZACOS9_IDLE_MINUTE_MS=str(MINUTE),
               ZACOS9_SUSPEND_COMMAND=f"echo slept >>'{slept}'",
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

            def states():
                try:
                    lines = state_file.read_text().split("\n")
                except FileNotFoundError:
                    return []
                return sorted(line.split()[2] for line in lines if line.startswith("output "))

            def sleeps():
                return slept.read_text().count("slept") if slept.exists() else 0

            def kept_for_a_while(expected, before):
                time.sleep(6 * MINUTE / 1000)  # past every timeout
                assert states() == expected, states()
                assert sleeps() == before, "the system slept while an app asked it not to"

            wait_for("zacos9-inhibit on the bus", lambda: has_owner(SAVER) and
                     has_owner("org.freedesktop.impl.portal.desktop.zacos9inhibit"))

            # A process with no window: every screen, and the system, stay awake.
            cookie = saver("Inhibit", GLib.Variant("(ss)", ("test", "no window")), "(u)")[0]
            assert cookie, "no cookie"
            kept_for_a_while(["awake", "awake"], 0)
            assert saver("GetActive", None, "(b)") == (False,)
            print("ok: Inhibit from a process with no window keeps every screen and the system awake")
            saver("UnInhibit", GLib.Variant("(u)", (cookie,)))
            wait_for("both asleep after UnInhibit", lambda: states() == ["asleep", "asleep"], 2)
            wait_for("the system asleep after UnInhibit", lambda: sleeps() == 1, 3)
            assert saver("GetActive", None, "(b)") == (True,)
            print("ok: UnInhibit lets them sleep; GetActive says so")
            saver("SimulateUserActivity")
            wait_for("SimulateUserActivity to wake them", lambda: states() == ["awake", "awake"], 2)
            print("ok: SimulateUserActivity wakes every screen")

            # An app with a window: only that window's screen.
            film = subprocess.Popen([client, "--dbus"], env=env, stdout=subprocess.PIPE,
                                    stderr=log, text=True)
            processes.append(film)
            line = film.stdout.readline()
            assert line.startswith("inhibited ") and line.split()[1] != "0", line
            saver("SimulateUserActivity")
            wait_for("its screen awake, the other asleep", lambda: states() == ["asleep", "awake"])
            kept_for_a_while(["asleep", "awake"], 1)
            print("ok: an app's request keeps only the screen showing its window, and the system, awake")
            film.terminate()
            film.wait(timeout=5)
            wait_for("both asleep once it quits", lambda: states() == ["asleep", "asleep"])
            wait_for("the system asleep once it quits", lambda: sleeps() == 2, 3)
            print("ok: its request ends when it leaves the bus")

            # The portal's Inhibit, as xdg-desktop-portal passes it on.
            handle = "/org/freedesktop/portal/desktop/request/1_1/test"
            call("org.freedesktop.impl.portal.desktop.zacos9inhibit", "/org/freedesktop/portal/desktop",
                 "org.freedesktop.impl.portal.Inhibit", "Inhibit",
                 GLib.Variant("(ossua{sv})", (handle, "", "", 8, {})))
            saver("SimulateUserActivity")
            before = sleeps()
            kept_for_a_while(["awake", "awake"], before)
            print("ok: the portal's idle Inhibit keeps them awake")
            call("org.freedesktop.impl.portal.desktop.zacos9inhibit", handle,
                 "org.freedesktop.impl.portal.Request", "Close")
            wait_for("both asleep once the request closes", lambda: states() == ["asleep", "asleep"], 2)
            wait_for("the system asleep once it closes", lambda: sleeps() > before, 3)
            print("ok: closing its Request ends it")
        except Exception:
            log.seek(0)
            print(log.read(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()

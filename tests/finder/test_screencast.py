#!/usr/bin/env python3
"""Negotiate a private ZacOS ScreenCast session; never share the real desktop."""

import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import time

from gi.repository import Gio, GLib


def main():
    backend = shutil.which("xdg-desktop-portal-wlr",
                           path="/usr/libexec:/usr/lib/xdg-desktop-portal")
    frontend = shutil.which("xdg-desktop-portal",
                            path="/usr/libexec:/usr/lib/xdg-desktop-portal")
    runtime = Path(os.environ.get("XDG_RUNTIME_DIR", "/nonexistent"))
    if not backend or not frontend or not (runtime / "pipewire-0").is_socket():
        print("SKIP: installed wlroots portal and a running PipeWire session are required")
        return 77
    renderer = os.environ.get("ZACOS9_SCREENCAST_TEST_RENDERER", "gles2")
    if renderer == "gles2" and not any(
            os.access(node, os.R_OK | os.W_OK) for node in Path("/dev/dri").glob("renderD*")):
        print("SKIP: a usable render node is required for the GLES2 capture session test")
        return 77
    with tempfile.TemporaryDirectory(prefix="zacos9-screencast-") as temporary:
        root = Path(temporary)
        config = root / "wlr.conf"
        config.write_text("[screencast]\nchooser_type=none\n")
        portal_config = root / "config/xdg-desktop-portal/portals.conf"
        portal_config.parent.mkdir(parents=True)
        portal_config.write_text("[preferred]\norg.freedesktop.impl.portal.ScreenCast=wlr\n")
        desktop_entry = root / "data/applications/org.zacos9.CaptureTest.desktop"
        desktop_entry.parent.mkdir(parents=True)
        desktop_entry.write_text(
            "[Desktop Entry]\nType=Application\nName=Capture Test\nExec=python3\n")
        env = dict(os.environ, HOME=temporary, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"), XDG_RUNTIME_DIR=temporary,
                   WAYLAND_DISPLAY="wayland-0", WLR_BACKENDS="headless",
                   WLR_RENDERER=renderer,
                   WLR_HEADLESS_OUTPUTS="1",
                   ZACOS9_FINDER="", ZACOS9_MENUBAR="", ZACOS9_COLLAR="",
                   ZACOS9_STARTUP="0", PIPEWIRE_REMOTE=str(runtime / "pipewire-0"))
        processes = []
        with (root / "capture.log").open("w+") as log:
            try:
                processes.append(subprocess.Popen([sys.argv[1], "-d"], env=env, stdout=log, stderr=log))
                deadline = time.monotonic() + 8
                while not (root / "wayland-0").is_socket():
                    if processes[0].poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError("private headless compositor did not start")
                    time.sleep(0.05)
                time.sleep(0.5)
                processes.append(subprocess.Popen(
                    [backend, "-c", str(config), "-l", "INFO"],
                    env=env, stdout=log, stderr=log))
                bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

                def wait_for_owner(destination, process):
                    deadline = time.monotonic() + 8
                    while True:
                        owned = bus.call_sync(
                            "org.freedesktop.DBus", "/org/freedesktop/DBus",
                            "org.freedesktop.DBus", "NameHasOwner",
                            GLib.Variant("(s)", (destination,)), None,
                            Gio.DBusCallFlags.NONE, 1000, None).unpack()[0]
                        if owned:
                            return
                        if process.poll() is not None or time.monotonic() >= deadline:
                            raise RuntimeError(f"private {destination} did not start")
                        time.sleep(0.05)

                wait_for_owner("org.freedesktop.impl.portal.desktop.wlr", processes[-1])

                processes.append(subprocess.Popen(
                    [frontend], env=env, stdout=log, stderr=log))
                destination = "org.freedesktop.portal.Desktop"
                wait_for_owner(destination, processes[-1])
                bus.call_sync(
                    destination, "/org/freedesktop/portal/desktop",
                    "org.freedesktop.host.portal.Registry", "Register",
                    GLib.Variant("(sa{sv})", ("org.zacos9.CaptureTest", {})),
                    None, Gio.DBusCallFlags.NONE, 15000, None)

                def request(method, signature, arguments):
                    responses = []

                    def response(connection, sender, path, interface, signal, parameters):
                        responses.append(parameters.unpack())

                    subscription = bus.signal_subscribe(
                        destination, "org.freedesktop.portal.Request", "Response",
                        None, None, Gio.DBusSignalFlags.NONE, response)
                    try:
                        bus.call_sync(
                            destination, "/org/freedesktop/portal/desktop",
                            "org.freedesktop.portal.ScreenCast", method,
                            GLib.Variant(signature, arguments), None,
                            Gio.DBusCallFlags.NONE, 15000, None)
                        deadline = time.monotonic() + 15
                        context = GLib.MainContext.default()
                        while not responses:
                            if time.monotonic() >= deadline:
                                raise RuntimeError(f"{method} did not respond")
                            context.iteration(False)
                            time.sleep(0.01)
                        if responses[0][0] != 0:
                            raise RuntimeError(f"{method} was refused: {responses[0][0]}")
                        return responses[0][1]
                    finally:
                        bus.signal_unsubscribe(subscription)

                session = request("CreateSession", "(a{sv})",
                                  ({"handle_token": GLib.Variant("s", "create"),
                                    "session_handle_token": GLib.Variant("s", "capture")},))["session_handle"]
                request("SelectSources", "(oa{sv})",
                        (session, {"types": GLib.Variant("u", 1),
                                   "cursor_mode": GLib.Variant("u", 1),
                                   "handle_token": GLib.Variant("s", "select")}))
                result = request("Start", "(osa{sv})",
                                 (session, "", {"handle_token": GLib.Variant("s", "start")}))
                streams = result["streams"]
                if len(streams) != 1 or not streams[0][0]:
                    raise RuntimeError(f"expected one valid stream, got {streams}")
                remote, descriptors = bus.call_with_unix_fd_list_sync(
                    destination, "/org/freedesktop/portal/desktop",
                    "org.freedesktop.portal.ScreenCast", "OpenPipeWireRemote",
                    GLib.Variant("(oa{sv})", (session, {})), None,
                    Gio.DBusCallFlags.NONE, 5000, None, None)
                fd = descriptors.get(remote.unpack()[0])
                try:
                    if not stat.S_ISSOCK(os.fstat(fd).st_mode):
                        raise RuntimeError("OpenPipeWireRemote did not return a socket")
                finally:
                    os.close(fd)
                bus.call_sync(
                    destination, session, "org.freedesktop.portal.Session", "Close",
                    None, None, Gio.DBusCallFlags.NONE, 5000, None)
                print("PASS: headless ZacOS ScreenCast session exports one stream and a PipeWire socket")
            except Exception:
                log.flush()
                log.seek(0)
                print(log.read(), file=sys.stderr)
                raise
            finally:
                for process in reversed(processes):
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=5)
    return 0


if __name__ == "__main__":
    sys.exit(main())

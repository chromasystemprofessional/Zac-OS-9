#!/usr/bin/python3
"""X11 programs read their preferences from the root window's resource
manager, which the compositor fills when Xwayland starts (issue #184:
XTerm opened white-on-black at its own size, ignoring the colors and
geometry in ~/.Xresources until the person ran xrdb by hand).

The first X11 program to connect must already see them. The compositor's
own resources (the ZacOS cursor theme and Xft.dpi: 96) must still follow
the person's, so a stale Xft.dpi cannot make X11 programs scale themselves
again (issue #33).

usage: test_xresources.py ZACOS9-WM XRESOURCES-CLIENT
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

if not shutil.which("Xwayland"):
    print("SKIP: Xwayland is needed to check the X11 resource manager")
    sys.exit(77)

compositor, resources_client = (Path(arg).resolve() for arg in sys.argv[1:3])

with tempfile.TemporaryDirectory(prefix="zacos9-xresources-") as directory:
    root = Path(directory)
    (root / ".Xresources").write_text(
        "XTerm*foreground: green\n"
        "XTerm*background: black\n"
        "XTerm*geometry: 100x40\n"
        "Xft.dpi: 120\n")
    env = dict(os.environ, HOME=directory, XDG_RUNTIME_DIR=directory, XDG_CONFIG_HOME=directory,
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
               ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="",
               ZACOS9_COLLAR="", ZACOS9_INHIBIT="",
               QT_STYLE_OVERRIDE="", QT_WAYLAND_DISABLE_WINDOWDECORATION="1",
               RESOURCES_DISPLAY=str(root / "display"))
    env.pop("WAYLAND_DISPLAY", None)
    with (root / "server.log").open("w+") as log:
        server = subprocess.Popen([compositor, "-s",
                                   'printf "%s" "$DISPLAY" > "$RESOURCES_DISPLAY"'],
                                  env=env, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                sockets = [p for p in root.glob("wayland-*") if not p.name.endswith(".lock")]
                if sockets and (root / "display").exists():
                    break
                assert server.poll() is None, "Compositor exited before creating its socket"
                time.sleep(0.02)
            assert sockets and (root / "display").exists(), "Compositor did not start"
            env["WAYLAND_DISPLAY"] = sockets[0].name
            env["DISPLAY"] = (root / "display").read_text()

            # The first X11 program, as an XTerm from the desktop would be.
            resources = subprocess.run([resources_client], env=env, check=True,
                                       capture_output=True, text=True, timeout=5).stdout
            assert "XTerm*foreground: green" in resources, resources
            assert "XTerm*background: black" in resources, resources
            assert "XTerm*geometry: 100x40" in resources, resources
            assert "Xft.dpi: 96" in resources, resources
            assert resources.rstrip().endswith("Xft.dpi: 96"), \
                "the compositor's Xft.dpi follows the person's, so it wins (issue #33)"
            print("ok: the first X11 program sees the person's X resources, under our own")
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
            log.seek(0)
            errors = [line for line in log.read().splitlines() if "[ERROR]" in line]
            if errors:
                print("compositor errors:", *errors, sep="\n  ", file=sys.stderr)

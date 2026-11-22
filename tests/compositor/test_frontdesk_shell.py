#!/usr/bin/env python3
"""A Front Desk session starts the menu bar, The Collar and
zacos9-frontdesk, not the Sniffer. The shell commands are stand-ins that
leave a stamp, so no Wayland client or Qt is needed."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

compositor = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="zacos9-frontdesk-shell-") as temp:
    root = Path(temp)
    (root / "home").mkdir()
    (root / "config").mkdir()
    (root / "data").mkdir()
    (root / "cache").mkdir()
    root.chmod(0o700)
    scripts = {}
    for name in ("menubar", "finder", "collar", "frontdesk"):
        path = root / f"run-{name}"
        path.write_text(
            "#!/bin/sh\n"
            f": > \"$TEST_DIR/{name}.ran\"\n"
            "sleep 5\n")
        path.chmod(0o700)
        scripts[name] = str(path)
    env = dict(os.environ,
        HOME=str(root / "home"), XDG_RUNTIME_DIR=str(root),
        XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
        XDG_CACHE_HOME=str(root / "cache"),
        WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
        ZACOS9_SHELL="frontdesk", ZACOS9_MENUBAR_MODE="frontdesk",
        ZACOS9_STARTUP="0", ZACOS9_INHIBIT="",
        ZACOS9_MENUBAR=scripts["menubar"], ZACOS9_FINDER=scripts["finder"],
        ZACOS9_COLLAR=scripts["collar"], ZACOS9_FRONTDESK=scripts["frontdesk"],
        TEST_DIR=str(root))
    env.pop("WAYLAND_DISPLAY", None)
    with (root / "wm.log").open("w+") as log:
        server = subprocess.Popen([str(compositor)], env=env, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                if (root / "menubar.ran").exists() and (root / "frontdesk.ran").exists():
                    break
                if server.poll() is not None:
                    break
                time.sleep(0.1)
            assert (root / "menubar.ran").exists(), (root / "wm.log").read_text()
            assert (root / "frontdesk.ran").exists(), (root / "wm.log").read_text()
            assert not (root / "finder.ran").exists(), "the Sniffer started"
            assert (root / "collar.ran").exists(), "the Collar didn't start"
        finally:
            server.terminate()
            server.wait(timeout=10)
print("ok: Front Desk replaces the Sniffer and keeps the Collar in its session")

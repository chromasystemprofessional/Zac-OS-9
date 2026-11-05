#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
import sys
import tempfile


session = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="zacos9-safe-mode-") as temp:
    root = Path(temp)
    bindir = root / "bin"
    bindir.mkdir()
    wm_log = root / "wm.log"
    locale_log = root / "locale.log"
    for name, body in {
        "zacos9-wm": '#!/bin/sh\nprintf "%s|%s\\n" "$LANG" "$LANGUAGE" > "$TEST_LOCALE_LOG"\n'
                    'while [ "$#" -gt 0 ]; do\n'
                    '[ "$1" = "-s" ] && { printf "%s\\n" "$2" > "$TEST_WM_LOG"; exit 0; }\n'
                    'shift\ndone\nexit 2\n',
        "zacos9-desktop-places": "#!/bin/sh\nexit 0\n",
    }.items():
        path = bindir / name
        path.write_text(body)
        path.chmod(0o700)
    # The person's Text panel choice wins over the installed system's locale.
    (root / ".config").mkdir()
    (root / ".config" / "locale.conf").write_text("LANG=fr_CA.UTF-8\nLANGUAGE=fr_CA\n")
    env = os.environ | {
        "HOME": str(root),
        "XDG_CONFIG_HOME": str(root / ".config"),
        "XDG_RUNTIME_DIR": str(root / "runtime"),
        "XDG_STATE_HOME": str(root / "state"),
        "DBUS_SESSION_BUS_ADDRESS": "unix:path=/unused",
        "WLR_RENDERER": "pixman",
        "PATH": f"{bindir}:{os.environ['PATH']}",
        "TEST_WM_LOG": str(wm_log),
        "TEST_LOCALE_LOG": str(locale_log),
    }
    for safe in (False, True):
        if safe:
            env["ZACOS9_SAFE_MODE"] = "1"
        else:
            env.pop("ZACOS9_SAFE_MODE", None)
        subprocess.run(["/bin/sh", str(session)], env=env, check=True, timeout=10)
        startup = wm_log.read_text()
        assert ("zacos9-autostart" not in startup) == safe, (safe, startup)
        assert locale_log.read_text() == "fr_CA.UTF-8|fr_CA\n", (safe, locale_log.read_text())

print("ok: safe mode skips Startup Items while normal sessions run them")
print("ok: the person's locale choice reaches the session")

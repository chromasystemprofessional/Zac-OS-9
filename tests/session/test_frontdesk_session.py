#!/usr/bin/env python3
"""The session reads /etc/zacos9/frontdesk/USER: it sets the shell and menu
bar modes and does not run the person's Startup Items."""
import os
from pathlib import Path
import pwd
import subprocess
import sys
import tempfile

session = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="zacos9-frontdesk-session-") as temp:
    root = Path(temp)
    bindir = root / "bin"
    bindir.mkdir()
    accounts = root / "accounts"
    (accounts / "frontdesk").mkdir(parents=True)
    frontdesk = accounts / "frontdesk" / pwd.getpwuid(os.getuid()).pw_name
    frontdesk.write_text("menus=restricted\nzac-menu=on\n")
    wm_log = root / "wm.log"
    shell_log = root / "shell.log"
    for name, body in {
        "zacos9-wm": '#!/bin/sh\n'
                     'printf "%s|%s|%s\\n" "$ZACOS9_SHELL" "$ZACOS9_MENUBAR_MODE" '
                     '"${ZACOS9_FRONTDESK_ZAC_MENU:-}" > "$TEST_SHELL_LOG"\n'
                     'while [ "$#" -gt 0 ]; do\n'
                     '[ "$1" = "-s" ] && { printf "%s\\n" "$2" > "$TEST_WM_LOG"; exit 0; }\n'
                     'shift\ndone\nexit 2\n',
        "zacos9-desktop-places": "#!/bin/sh\nexit 0\n",
        "flatpak": "#!/bin/sh\nexit 0\n",
    }.items():
        path = bindir / name
        path.write_text(body)
        path.chmod(0o700)
    env = os.environ | {
        "HOME": str(root),
        "XDG_CONFIG_HOME": str(root / ".config"),
        "XDG_RUNTIME_DIR": str(root / "runtime"),
        "XDG_STATE_HOME": str(root / "state"),
        "DBUS_SESSION_BUS_ADDRESS": "unix:path=/unused",
        "ZACOS9_ACCOUNTS_CONF": str(accounts),
        "PATH": f"{bindir}:{os.environ['PATH']}",
        "TEST_WM_LOG": str(wm_log),
        "TEST_SHELL_LOG": str(shell_log),
    }
    subprocess.run(["/bin/sh", str(session)], env=env, check=True, timeout=15)
    assert shell_log.read_text() == "frontdesk|frontdesk|on\n", shell_log.read_text()
    startup = wm_log.read_text()
    assert "zacos9-autostart" not in startup, startup

    # Without the setting, the menu bar keeps its Front Desk Zac menu.
    frontdesk.write_text("menus=restricted\n")
    subprocess.run(["/bin/sh", str(session)], env=env, check=True, timeout=15)
    assert shell_log.read_text() == "frontdesk|frontdesk|\n", shell_log.read_text()
print("ok: a Front Desk account starts the Front Desk shell without Startup Items, "
      "with all Zac menu items only when asked")

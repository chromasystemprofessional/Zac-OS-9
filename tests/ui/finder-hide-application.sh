#!/bin/sh
# UI test: queue an application in Trash and restore it (never uninstall it).
# Run inside WSL:
#   scripts/snapshot.sh /tmp/hide.png 14 "sh tests/ui/finder-hide-application.sh"
# Expected:
#   hide1.png  Applications with the queued application's launcher gone
#   hide2.png  Trash holding the queued application
#   hide3.png  Applications after Put Away cancels removal
# Use an otherwise empty Trash in the disposable UI-test session.
V=build/shell/vptr
choose() { # $1 = menu title x, $2 = item y
	wlrctl pointer move -3000 -3000
	wlrctl pointer move "$1" 9; sleep 0.2
	wlrctl pointer click left; sleep 0.3
	wlrctl pointer move 0 $(($2 - 9)); sleep 0.2
	wlrctl pointer click left; sleep 1
}
sleep 4
$V home move 1240 50 click wait 150 click          # Macintosh HD
sleep 2
$V home move 173 110 click wait 150 click          # Applications
sleep 2
$V home move 353 130 click                         # select (not open) an app folder
sleep 1
choose 50 60                                        # File > Move To Trash
grim /tmp/hide1.png
build/shell/zacos9-finder --open "file://${XDG_DATA_HOME:-$HOME/.local/share}/Trash/files"
sleep 1
grim /tmp/hide2.png
python3 - <<'PY'
import os, socket
path = (os.environ.get("XDG_RUNTIME_DIR", "/tmp") + "/zacos9-finder." +
        os.environ.get("WAYLAND_DISPLAY", "wayland-0") + ".sock")
with socket.socket(socket.AF_UNIX) as client:
    client.connect(path)
    client.recv(4096)
    client.sendall(b"cmd select-all\ncmd put-away\ncmd close-window\n")
PY
sleep 1
grim /tmp/hide3.png

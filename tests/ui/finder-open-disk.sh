#!/bin/sh
# UI test: the Unix disk, the power-user view of Debian's own filesystem.
# The startup disk now shows the Macintosh view (tests/ui/finder-startup-disk.sh);
# this is the second disk, which is off until the registry asks for it.
#
# Run inside WSL:
#   python3 - <<'PY'
#   import json, os
#   p = os.path.expanduser('~/.local/share/platinum/finder/vfs.json')
#   r = json.load(open(p)); r['showUnixVolume'] = True
#   json.dump(r, open(p, 'w'), indent=2)
#   PY
#   scripts/snapshot.sh /tmp/unix.png 4 "sh tests/ui/finder-open-disk.sh"
#
# Expected: a second disk, "Unix", below "Macintosh HD" on the desktop,
# opening to "N items, X available" and an icon grid of / (usr, etc, bin,
# var, ...). Set showUnixVolume back to false afterwards.
V=build/shell/vptr
sleep 4
$V home move 1240 114 click wait 150 click   # double-click Unix
sleep 3

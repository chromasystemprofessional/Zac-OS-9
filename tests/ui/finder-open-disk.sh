#!/bin/sh
# UI test: double-clicking Hard Disk on the desktop opens its Finder window.
# Run inside WSL:  scripts/snapshot.sh /tmp/finder.png 6 "sh tests/ui/finder-open-disk.sh"
# Expected: a "Hard Disk" window with "N items, X available" and an icon grid of /.
sleep 3
wlrctl pointer move -3000 -3000
wlrctl pointer move 1240 50
wlrctl pointer click left; wlrctl pointer click left   # double-click Hard Disk
sleep 1.5
grim /tmp/finder1.png

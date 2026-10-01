#!/bin/sh
# UI test: the startup screen while the menu bar and Finder load.
# Run inside WSL:
#   scripts/snapshot.sh /tmp/su.png 1 "sh tests/ui/startup-screen.sh"
# Expected:
#   /tmp/su1.png  the Welcome box over the desktop pattern, bar starting
#   /tmp/su2.png  the bar further along (or the desktop, on a fast machine)
#   /tmp/su3.png  the desktop with menu bar and icons; the box has lifted
sleep 0.3
grim /tmp/su1.png
sleep 0.9
grim /tmp/su2.png
sleep 2.8
grim /tmp/su3.png

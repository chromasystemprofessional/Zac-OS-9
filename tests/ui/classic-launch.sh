#!/bin/sh
# UI test: logo menu > Classic starts SheepShaver in a Platinum window.
# Setup and run inside WSL (~/Classic holds the user's ROMs, here a link):
#   ln -sfn "$PWD/macos" ~/Classic
#   scripts/snapshot.sh /tmp/cl.png 12 "sh tests/ui/classic-launch.sh"
# Expected:
#   /tmp/cl1.png  the logo menu with "Classic" highlighted
#   /tmp/cl.png   a "SheepShaver" window: the Mac's gray screen, with the
#                 flashing "?" disk while there is no system disk
# With PLATINUM_CLASSIC_DIR pointing at an empty folder, an alert explains
# what's missing instead.
sleep 3
wlrctl pointer move -3000 -3000; wlrctl pointer move 22 9
wlrctl pointer click left; sleep 0.3
wlrctl pointer move 0 41; sleep 0.4
grim /tmp/cl1.png
wlrctl pointer click left

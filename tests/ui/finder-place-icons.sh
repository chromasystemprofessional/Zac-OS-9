#!/bin/sh
# UI test: icons stay where you drag them, across sessions.
# Setup and run inside WSL (two sessions):
#   rm -rf ~/Desktop/* ~/.local/share/platinum/finder; echo hi > ~/Desktop/Report.txt
#   scripts/snapshot.sh /tmp/pl1.png 4 "sh tests/ui/finder-place-icons.sh 1"
#   scripts/snapshot.sh /tmp/pl2.png 3 "sh tests/ui/finder-place-icons.sh 2"
# Expected: Report.txt sits mid-screen (about 600,400) in both shots.
V=build/shell/vptr
sleep 3
if [ "$1" = 1 ]; then
	# Report.txt starts in the first slot below the disk, centre (1240,114).
	$V home move 1240 114 click wait 800 down move -20 0 wait 100 move -620 286 wait 300 up wait 800
fi

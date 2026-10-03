#!/bin/sh
# UI test: spatial windows remember place, size and view across sessions.
# Setup and run inside WSL (two sessions):
#   rm -rf ~/Desktop/* ~/.local/share/zacos9/finder; mkdir -p ~/Desktop/Forms/Internal
#   scripts/snapshot.sh /tmp/sp1.png 9 "sh tests/ui/finder-spatial.sh 1"
#   scripts/snapshot.sh /tmp/sp2.png 4 "sh tests/ui/finder-spatial.sh 2"
# Expected: /tmp/sp1.png and /tmp/sp2.png both show the Forms window moved
# right and down, in list view; the second session put it there itself.
V=build/shell/vptr
sleep 3
$V home move 1240 114 click wait 100 click wait 1500       # open Forms
if [ "$1" = 1 ]; then
	# Drag the title bar 300 right, 200 down; then View > as List.
	$V home move 200 49 down move 300 200 wait 200 up wait 800
	wlrctl pointer move -3000 -3000; wlrctl pointer move 137 9
	wlrctl pointer click left; sleep 0.3
	wlrctl pointer move 0 51; wlrctl pointer click left; sleep 1
fi

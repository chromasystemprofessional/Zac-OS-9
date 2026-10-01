#!/bin/sh
# UI test: drag and drop in the Finder (desktop at 1280x720).
# Setup and run inside WSL:
#   rm -rf ~/Desktop/Docs ~/Desktop/Report.txt; mkdir -p ~/Desktop/Docs
#   echo hi > ~/Desktop/Report.txt
#   scripts/snapshot.sh /tmp/dnd.png 12 "sh tests/ui/finder-drag.sh"
# Expected:
#   /tmp/dnd0.png  mid-drag: dotted outline over Docs, Docs highlighted
#   /tmp/dnd1.png  Report.txt gone from the desktop
#   /tmp/dnd2.png  the Docs window shows Report.txt
#   /tmp/dnd3.png  Docs gone, Trash full
V=build/shell/vptr
sleep 3
# Desktop icon centres: Docs (1240,114), Report.txt (1240,178), Trash (1240,656).
$V home move 1240 178 down move -20 0 wait 100 move 20 -64 wait 400
grim /tmp/dnd0.png
$V up wait 800
grim /tmp/dnd1.png
$V home move 1240 114 click wait 100 click wait 1500
grim /tmp/dnd2.png
$V home move 1240 114 down move -20 0 wait 100 move 20 542 wait 300 up wait 1000
grim /tmp/dnd3.png

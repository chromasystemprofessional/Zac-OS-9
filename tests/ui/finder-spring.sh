#!/bin/sh
# UI test: spring-loaded folders.
# Setup and run inside WSL:
#   rm -rf ~/Desktop/* ~/.local/share/platinum/finder; mkdir ~/Desktop/Docs
#   echo hi > ~/Desktop/Report.txt
#   scripts/snapshot.sh /tmp/spr.png 9 "sh tests/ui/finder-spring.sh"
# Expected:
#   /tmp/spr1.png  mid-drag: the Docs window has sprung open
#   /tmp/spr2.png  after the drop: Report.txt gone from the desktop, the
#                  sprung window closed again; ~/Desktop/Docs/Report.txt exists
V=build/shell/vptr
sleep 3
# Desktop icons: Docs (1240,114), Report.txt (1240,178).
$V home move 1240 178 down move -20 0 wait 100 move 20 -64 wait 1600
grim /tmp/spr1.png
$V move -1040 36 wait 400 up wait 1000
grim /tmp/spr2.png

#!/bin/sh
# UI test: File > Find… searches the disk by name.
# Setup and run inside WSL:
#   rm -rf ~/Desktop/* ~/.local/share/platinum/finder; mkdir ~/Desktop/Docs
#   echo hi > "$HOME/Desktop/Docs/Quarterly Zebra.txt"; echo hi > ~/Desktop/zebra-notes.md
#   scripts/snapshot.sh /tmp/fd.png 14 "sh tests/ui/finder-find.sh"
# Expected:
#   /tmp/fd1.png  the Find File window with "zebra" typed
#   /tmp/fd2.png  Items Found: both files
#   /tmp/fd3.png  the first selected, and the
#                 bottom pane shows where it is
V=build/shell/vptr
sleep 3
wlrctl pointer move -3000 -3000; wlrctl pointer move 50 9
wlrctl pointer click left; sleep 0.3
wlrctl pointer move 0 175; wlrctl pointer click left; sleep 1.2   # File > Find…
wlrctl keyboard type "zebra"; sleep 0.6
grim /tmp/fd1.png
$V home move 385 139 click wait 4000                    # Find
grim /tmp/fd2.png
$V home move 120 133 click wait 600                      # first result
grim /tmp/fd3.png

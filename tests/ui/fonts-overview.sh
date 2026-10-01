#!/bin/sh
# UI test: Platinum's own fonts across the desktop.
# Setup and run inside WSL:
#   rm -rf ~/Desktop/* ~/.local/share/platinum/finder
#   mkdir -p ~/Desktop/Documents/Letters; echo hi > ~/Desktop/Documents/Report.txt
#   ln -s ~/Desktop/Documents ~/Desktop/"Documents alias"
#   scripts/snapshot.sh /tmp/fo.png 6 "sh tests/ui/fonts-overview.sh"
# Expected:
#   /tmp/fo1.png  menu bar and Finder window in the system font; icon
#                 labels in the views font, "Documents alias" in italics
#   /tmp/fo.png   the File menu open (system font, ⌘ shortcuts)
V=build/shell/vptr
sleep 3
$V home move 1240 114 click wait 100 click wait 1500      # open Documents
grim /tmp/fo1.png
$V home move 50 9 click wait 600

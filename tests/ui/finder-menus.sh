#!/bin/sh
# UI test: Finder menu commands from the menu bar.
# Run inside WSL:  scripts/snapshot.sh /tmp/fm.png 12 "sh tests/ui/finder-menus.sh"
# Expected:
#   /tmp/fm1.png  an "untitled folder" icon on the desktop, selected
#   /tmp/fm2.png  the folder gone, the Trash icon full
#   /tmp/fm3.png  the Trash icon empty again
sleep 3
choose() { # $1 = title x, $2 = item y
	wlrctl pointer move -3000 -3000
	wlrctl pointer move "$1" 9; sleep 0.2
	wlrctl pointer click left; sleep 0.3
	wlrctl pointer move 0 $(($2 - 9)); sleep 0.2
	wlrctl pointer click left; sleep 1
}
wlrctl pointer move -3000 -3000
wlrctl pointer move 600 400
wlrctl pointer click left; sleep 0.5   # desktop: the Finder comes forward
choose 50 28                            # File > New Folder
grim /tmp/fm1.png
choose 50 60                            # File > Move To Trash
grim /tmp/fm2.png
choose 190 28                           # Special > Empty Trash
grim /tmp/fm3.png

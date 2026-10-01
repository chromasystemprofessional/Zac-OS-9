#!/bin/sh
# UI test: renaming a desktop icon in place.
# Setup and run inside WSL:
#   rm -rf ~/Desktop/*; echo hi > ~/Desktop/Report.txt
#   scripts/snapshot.sh /tmp/rn.png 8 "sh tests/ui/finder-rename.sh"
# Expected:
#   /tmp/rn1.png  "Report.txt" in an edit box, all selected (lavender)
#   /tmp/rn2.png  "Notes.tx" being typed, caret visible
#   /tmp/rn3.png  the icon named "Notes.txt"; ~/Desktop/Notes.txt exists
sleep 3
wlrctl pointer move -3000 -3000
wlrctl pointer move 1240 114
wlrctl pointer click left; sleep 0.8     # select Report.txt
wlrctl keyboard type "
"; sleep 0.5                              # Return: edit the name
grim /tmp/rn1.png
wlrctl keyboard type "Notes.tx"; sleep 0.3
grim /tmp/rn2.png
wlrctl keyboard type "t
"; sleep 1                                # finish and press Return
grim /tmp/rn3.png

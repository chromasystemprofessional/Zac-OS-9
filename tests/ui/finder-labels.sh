#!/bin/sh
# UI test: File > Label submenu sets a Finder label (user.zacos9.label).
# Setup and run inside WSL:
#   rm -rf ~/Desktop/* ~/.local/share/zacos9/finder; echo hi > ~/Desktop/Report.txt
#   scripts/snapshot.sh /tmp/lb.png 9 "sh tests/ui/finder-labels.sh"
#   python3 -c "import os; print(os.getxattr(os.path.expanduser('~/Desktop/Report.txt'), 'user.zacos9.label'))"
# Expected:
#   /tmp/lb1.png  File menu open, "Label" highlighted, its submenu beside
#                 it: None, separator, seven labels with color swatches
#   /tmp/lb2.png  "Hot" highlighted in the submenu
#   /tmp/lb3.png  Report.txt's icon tinted red; the attribute reads b'2'
V=build/shell/vptr
sleep 3
$V home move 1240 114 click wait 700                     # select Report.txt
wlrctl pointer move -3000 -3000; wlrctl pointer move 50 9
wlrctl pointer click left; sleep 0.3                     # File (sticky)
wlrctl pointer move 0 105; sleep 0.5                     # Label
grim /tmp/lb1.png
wlrctl pointer move 150 0; sleep 0.2                     # into the submenu
wlrctl pointer move 0 38; sleep 0.4                      # Hot
grim /tmp/lb2.png
wlrctl pointer click left; sleep 1.2
grim /tmp/lb3.png

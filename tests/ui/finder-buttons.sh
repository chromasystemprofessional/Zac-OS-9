#!/bin/sh
# UI test: button view; clicking a button opens it.
# Setup and run inside WSL:
#   rm -rf ~/Desktop/* ~/.local/share/zacos9/finder
#   mkdir -p ~/Desktop/Forms/External ~/Desktop/Forms/Internal
#   scripts/snapshot.sh /tmp/bt.png 8 "sh tests/ui/finder-buttons.sh"
# Expected:
#   /tmp/bt1.png  "Forms" as bevel buttons (External, Internal), names below
#   /tmp/bt2.png  after one click on Internal's button, the Internal window
V=build/shell/vptr
sleep 3
$V home move 1240 114 click wait 100 click wait 1500          # open Forms
wlrctl pointer move -3000 -3000; wlrctl pointer move 137 9
wlrctl pointer click left; sleep 0.3
wlrctl pointer move 0 35; wlrctl pointer click left; sleep 1  # View > as Buttons
grim /tmp/bt1.png
$V home move 180 116 click wait 1500                          # Internal's button
grim /tmp/bt2.png

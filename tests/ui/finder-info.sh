#!/bin/sh
# UI test: Get Info and About This Computer from the menus.
# Setup and run inside WSL:
#   rm -rf ~/Desktop/*; echo hello > ~/Desktop/Notes.txt
#   scripts/snapshot.sh /tmp/info.png 6 "sh tests/ui/finder-info.sh"
# Expected:
#   /tmp/info1.png  "Notes.txt Info": icon, name, Kind, Size, Where, dates
#   /tmp/info2.png  "About This Computer": logo, version, memory
sleep 3
choose() { # $1 = title x, $2 = item y
	wlrctl pointer move -3000 -3000
	wlrctl pointer move "$1" 9; sleep 0.2
	wlrctl pointer click left; sleep 0.3
	wlrctl pointer move 0 $(($2 - 9)); sleep 0.2
	wlrctl pointer click left; sleep 1
}
wlrctl pointer move -3000 -3000
wlrctl pointer move 1240 114
wlrctl pointer click left; sleep 0.8     # select Notes.txt
choose 50 98                              # File > Get Info
grim /tmp/info1.png
choose 22 28                              # logo menu > About This Computer
grim /tmp/info2.png

#!/bin/sh
# UI test: list view with a disclosure triangle (recreates HIG figure 2-24).
# Setup and run inside WSL:
#   rm -rf ~/Desktop/*; mkdir -p ~/Desktop/Forms/External ~/Desktop/Forms/Internal
#   echo a > ~/Desktop/Forms/Internal/Promotion; echo b > ~/Desktop/Forms/Internal/Vacation
#   scripts/snapshot.sh /tmp/list.png 7 "sh tests/ui/finder-list.sh"
# Expected:
#   /tmp/list1.png  "Forms" window as a list: Name (sorted, dark), Date
#                   Modified, Size, Kind; External and Internal with triangles
#   /tmp/list2.png  Internal expanded: Promotion and Vacation indented below
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
wlrctl pointer click left; wlrctl pointer click left; sleep 1.5   # open Forms
choose 137 60                                                    # View > as List
grim /tmp/list1.png
# Internal is the second row; its triangle sits at client x 12..18.
wlrctl pointer move -3000 -3000
wlrctl pointer move 61 132
wlrctl pointer click left; sleep 1
grim /tmp/list2.png

#!/bin/sh
# UI test: Duplicate, Make Alias, Move To Trash and Put Away (File menu).
# Setup and run inside WSL:
#   rm -rf ~/Desktop/* ~/.local/share/platinum/finder ~/.local/share/Trash/*/*
#   echo hi > ~/Desktop/Report.txt
#   scripts/snapshot.sh /tmp/fc.png 14 "sh tests/ui/finder-file-commands.sh"
# Expected:
#   /tmp/fc1.png  desktop: Report.txt, "Report.txt copy", "Report.txt alias"
#   /tmp/fc2.png  the alias is in the Trash window
#   /tmp/fc3.png  after Put Away, the alias is back on the desktop
#   ~/Desktop/Report.txt alias is a symbolic link to Report.txt
V=build/shell/vptr
sleep 3
file_menu() { # $1 = item y
	wlrctl pointer move -3000 -3000; wlrctl pointer move 50 9
	wlrctl pointer click left; sleep 0.3
	wlrctl pointer move 0 $(($1 - 9)); wlrctl pointer click left; sleep 1
}
select_at() { $V home move "$1" "$2" click wait 700; }

select_at 1240 114; file_menu 114        # Report.txt > Duplicate
select_at 1240 114; file_menu 130        # Report.txt > Make Alias
grim /tmp/fc1.png
file_menu 60                             # the new alias (selected) > Move To Trash
$V home move 1240 656 click wait 100 click wait 1500   # open the Trash
grim /tmp/fc2.png
select_at 94 108; file_menu 146          # alias in the Trash window > Put Away
grim /tmp/fc3.png

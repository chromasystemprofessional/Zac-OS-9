#!/bin/sh
# UI test: hiding an application from the Desktop (never uninstalling it).
# Run inside WSL:
#   scripts/snapshot.sh /tmp/hide.png 14 "sh tests/ui/finder-hide-application.sh"
# Expected:
#   hide1.png  the confirmation alert, naming the application, offering
#              "Remove" and "Cancel" (never "Delete" or "Uninstall")
#   hide2.png  Applications with that application's folder gone
#   hide3.png  Applications with it back, after Special > Show All Applications
V=build/shell/vptr
choose() { # $1 = menu title x, $2 = item y
	wlrctl pointer move -3000 -3000
	wlrctl pointer move "$1" 9; sleep 0.2
	wlrctl pointer click left; sleep 0.3
	wlrctl pointer move 0 $(($2 - 9)); sleep 0.2
	wlrctl pointer click left; sleep 1
}
sleep 4
$V home move 1240 50 click wait 150 click          # Macintosh HD
sleep 2
$V home move 173 110 click wait 150 click          # Applications
sleep 2
$V home move 353 130 click                         # select (not open) an app folder
sleep 1
choose 50 60                                        # File > Move To Trash
grim /tmp/hide1.png
$V key Return                                       # the alert's default button, "Remove"
sleep 1
grim /tmp/hide2.png
choose 190 44                                       # Special > Show All Applications
sleep 1
grim /tmp/hide3.png

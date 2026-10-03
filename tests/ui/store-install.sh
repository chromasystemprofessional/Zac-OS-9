#!/bin/sh
# UI test: the Software window (Apple menu > Software), end to end
# against the real system (installs and removes a real, small package).
# Run inside WSL:
#   scripts/snapshot.sh /tmp/store.png 50 "sh tests/ui/store-install.sh"
# Expected, in order:
#   store1.png  Software open, "Featured" selected, Firefox's details
#               (icon, blurb, "Not installed.", an "Install" button)
#   store2.png  the Utilities category, Calculator selected
#   store3.png  mid-install: the button disabled, a sweeping progress bar
#   store4.png  "Installed.", a "Remove" button, and the package's own
#               real icon in place of the generic one
#   store5.png  the removal confirmation alert, naming the package,
#               offering "Remove" and "Cancel"
#   store6.png  back to "Not installed." and the generic icon
#
# The coordinates are for the default 1280x720 nested screen, with the
# window opening at its first-run place. galculator is installed and
# removed for real; nothing is left behind either way.
V=build/shell/vptr
choose() { # $1 = menu title x, $2 = item y
	wlrctl pointer move -3000 -3000
	wlrctl pointer move "$1" 9; sleep 0.2
	wlrctl pointer click left; sleep 0.3
	wlrctl pointer move 0 $(($2 - 9)); sleep 0.2
	wlrctl pointer click left; sleep 1
}
click() { # $1 $2 = screen position
	wlrctl pointer move -3000 -3000
	wlrctl pointer move "$1" "$2"; sleep 0.2
	wlrctl pointer click left; sleep 1
}
sleep 4
choose 22 57                  # logo menu > Software
grim /tmp/store1.png
click 83 176                  # Utilities
click 230 112                 # Calculator
grim /tmp/store2.png
click 510 441                 # Install
grim /tmp/store3.png
sleep 15                      # apt installing galculator
grim /tmp/store4.png
click 510 441                 # Remove
grim /tmp/store5.png
$V key Return                 # the alert's default button, "Remove"
sleep 15                      # apt removing it
grim /tmp/store6.png

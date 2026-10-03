#!/bin/sh
# UI test: Edit menu commands reach the front app.
# Run inside WSL:  scripts/snapshot.sh /tmp/edit.png 9 "mousepad" "sh tests/ui/edit-menu.sh"
# Expected result: Mousepad shows "Hello ZacOS 9. Hello ZacOS 9."
# (typed text, then Select All, Copy, Paste, Paste via the menu bar).
sleep 2
wlrctl keyboard type "Hello ZacOS 9. "
sleep 0.5
pick() { # $1 = item y; pointer starts at the Edit title
	wlrctl pointer click left; sleep 0.3
	wlrctl pointer move 0 $(($1 - 9)); sleep 0.2
	wlrctl pointer click left; sleep 0.5
	wlrctl pointer move 0 $((9 - $1)); sleep 0.2
}
wlrctl pointer move -3000 -3000
wlrctl pointer move 85 9
pick 114   # Select All
pick 66    # Copy
pick 82    # Paste
pick 82    # Paste

#!/bin/sh
# UI test: File > Get Info > Sharing… on a desktop folder, then its icon.
# Run inside WSL as root, with file sharing on (sharing.conf afp=yes):
#   mkdir -p ~/Desktop/Kinds
#   scripts/snapshot.sh /tmp/sm-end.png 2 "sh tests/ui/finder-sharing-menu.sh"
# Expected:
#   /tmp/sm1.png  the File menu's Get Info submenu: General Information ⌘I,
#                 Sharing…
#   /tmp/sm2.png  "Kinds Info" open at Sharing
#   /tmp/sm3.png  after sharing it and closing the window, Kinds on the
#                 desktop has the shared-folder icon
V=build/shell/vptr
# The Finder that platinum-wm starts finds the helper in the sources.
PLATINUM_SHARING_HELPER="$PWD/sharing/platinum-sharing-helper"
sh "$PLATINUM_SHARING_HELPER" unshare "$HOME/Desktop/Kinds" 2>/dev/null

click_in() { # x y in the front window's content
	grim /tmp/sm-where.png
	set -- $(python3 tests/ui/content-origin.py /tmp/sm-where.png) "$1" "$2"
	$V home move $(($1 + $3)) $(($2 + $4)) click wait 700
}

sleep 5
$V home move 1240 114 click wait 1200 click wait 800   # select Kinds on the desktop
$V home move 50 9 down wait 400 move 0 89 wait 600 move 200 0 wait 500
grim /tmp/sm1.png
$V move 0 16 wait 300 up wait 2000              # Sharing…
grim /tmp/sm2.png
click_in 26 119                                 # Share this item and its contents
$V cmd w wait 3000
grim /tmp/sm3.png
grep Kinds /etc/platinum/shared-folders
sh "$PLATINUM_SHARING_HELPER" unshare "$HOME/Desktop/Kinds"

#!/bin/sh
# UI test: the startup disk shows the Macintosh view of this computer.
# Run inside WSL:
#   scripts/snapshot.sh /tmp/vfs1.png 3 "sh tests/ui/finder-startup-disk.sh"
#   scripts/snapshot.sh /tmp/vfs2.png 3 "sh tests/ui/finder-startup-disk.sh apps"
#   scripts/snapshot.sh /tmp/vfs3.png 3 "sh tests/ui/finder-startup-disk.sh system"
#   scripts/snapshot.sh /tmp/vfs4.png 6 "sh tests/ui/finder-startup-disk.sh launch"
# Expected:
#   vfs1: a "Macintosh HD" window, "3 items", holding System Folder (with
#         the sparkle on its icon), Applications and Documents, and no
#         Unix directories at all.
#   vfs2: inside Applications, one folder per installed application.
#   vfs3: inside the System Folder: Appearance, Control Panels (with the
#         sliders on its icon), Extensions, Fonts, Preferences.
#   vfs4: the application started, and the menu bar showing its name.
#
# The coordinates are for the default 1280x720 nested screen, with the
# windows opening at their first-run places.
V=build/shell/vptr
sleep 4
$V home move 1240 50 click wait 150 click          # open Macintosh HD
sleep 2
case "${1:-}" in
apps)
	$V home move 173 110 click wait 150 click  # Applications
	;;
system)
	$V home move 93 110 click wait 150 click   # System Folder
	;;
launch)
	$V home move 173 110 click wait 150 click  # Applications
	sleep 2
	$V home move 353 130 click wait 150 click  # an application's folder
	sleep 2
	$V home move 133 148 click wait 150 click  # the launcher inside it
	;;
esac
sleep 3

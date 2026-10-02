#!/bin/sh
# UI test: Get Info's Sharing view shares a folder and sets its privileges.
# Run inside WSL as root (the helper then runs directly, without pkexec):
#   scripts/snapshot.sh /tmp/share-end.png 2 "sh tests/ui/finder-sharing.sh"
# Expected:
#   /tmp/share1.png  "Projects Info": icon and name, General Information in
#                    the box the Show pop-up titles
#   /tmp/share2.png  Sharing: Where, "Share this item and its contents",
#                    Owner / User/Group / Everyone, each privilege an icon
#                    (glasses, pencil) beside a small pop-up button; Copy
#   /tmp/share-menu.png  the privilege menu, an icon before each choice
#   /tmp/share3.png  the box checked, Everyone set to Read only, and the note
#                    "This folder will be shared when you close this window."
#   then: /etc/platinum/shared-folders lists /root/Projects, which is 754.
V=build/shell/vptr
# The Finder that platinum-wm starts finds the helper in the sources.
PLATINUM_SHARING_HELPER="$PWD/sharing/platinum-sharing-helper"
rm -rf /root/Projects
mkdir -p /root/Projects
chmod 700 /root/Projects
grep -v "	/root/Projects\$" /etc/platinum/shared-folders >/tmp/sf 2>/dev/null
cat /tmp/sf >/etc/platinum/shared-folders 2>/dev/null

click_in() { # x y in the front window's content
	grim /tmp/share-where.png
	set -- $(python3 tests/ui/content-origin.py /tmp/share-where.png) "$1" "$2"
	$V home move $(($1 + $3)) $(($2 + $4)) click wait 700
}

sleep 5 # platinum-wm starts the Finder
python3 - <<'EOF'
import os, socket
s = socket.socket(socket.AF_UNIX)
s.connect(os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"),
                       "platinum-finder.%s.sock" % os.environ["WAYLAND_DISPLAY"]))
s.sendall(b"cmd info /root/Projects\n")
EOF
sleep 2
grim /tmp/share1.png
click_in 150 65     # Show: pop-up (opens sticky)
click_in 150 81     # its second item, Sharing (menu rows are 16 px)
sleep 1
grim /tmp/share2.png
click_in 26 119     # Share this item and its contents
click_in 302 290    # Everyone's privilege: the arrows beside the icon
grim /tmp/share-menu.png
click_in 330 258    # Read only: two rows above None, the current choice
sleep 0.5
grim /tmp/share3.png
$V cmd w wait 2000  # close: the changes are made
sleep 3
cat /etc/platinum/shared-folders
stat -c '%a %U:%G %n' /root/Projects

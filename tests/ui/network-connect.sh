#!/bin/sh
# UI test: the Network Browser, end to end against a real AFP server
# (mounts and ejects a real volume; see docs/network.md for what this
# could, and couldn't, verify live — SMB's success path in particular).
# Run inside WSL, against the AFP test server from tests/sharing/afp-client.sh
# (platest / Sesame-42, with File Sharing's AFP turned on so it's
# Bonjour-discoverable — see docs/network.md's note on Netatalk's own
# avahi announcement):
#   scripts/snapshot.sh /tmp/net.png 60 "sh tests/ui/network-connect.sh"
# Expected, in order:
#   net1.png  the Network Browser's server list, "Platinum Test (AFP)"
#             found by itself (no Connect to Server needed)
#   net2.png  the login dialog, Name and Password filled in
#   net3.png  the volume list ("Public", "platest's home")
#   net4.png  back at the desktop: "platest's home" mounted as a disk
#             icon, next to Macintosh HD
#   net5.png  gone again, after Put Away (Cmd-Y) on the selected icon
#
# The coordinates are for the default 1280x720 nested screen, with the
# Network Browser opening at its first-run place. The AFP connection was
# measured, live, at several seconds end to end in this project's own
# WSL development container (see docs/network.md); the waits below are
# generous for that reason and may be shortened on a real installation.
V=build/shell/vptr
sleep 4
wlrctl pointer move -3000 -3000; wlrctl pointer move 22 9      # logo menu
wlrctl pointer click left; sleep 0.3
wlrctl pointer move 0 72; sleep 0.3                             # > Network Browser
wlrctl pointer click left
sleep 5                                                         # the first scan
grim /tmp/net1.png

# double-click "Platinum Test (AFP)" (2nd row; 1st is this container's
# own Windows discovery self-answer, see docs/network.md)
wlrctl pointer move -3000 -3000; wlrctl pointer move 130 118
wlrctl pointer click left; wlrctl pointer click left
sleep 2
wlrctl pointer move -3000 -3000; wlrctl pointer move 498 274   # Registered User
wlrctl pointer click left; sleep 0.3
wlrctl pointer move -3000 -3000; wlrctl pointer move 670 303   # Name
wlrctl pointer click left; sleep 0.3
$V type platest
wlrctl pointer move -3000 -3000; wlrctl pointer move 670 331   # Password
wlrctl pointer click left; sleep 0.3
$V type Sesame-42
grim /tmp/net2.png
wlrctl pointer move -3000 -3000; wlrctl pointer move 749 370   # Connect
wlrctl pointer click left
sleep 13                                                        # see docs/network.md
grim /tmp/net3.png

wlrctl pointer move -3000 -3000; wlrctl pointer move 600 244   # "platest's home"
wlrctl pointer click left; sleep 0.3
wlrctl pointer move -3000 -3000; wlrctl pointer move 721 401   # Mount
wlrctl pointer click left
sleep 15
grim /tmp/net4.png

wlrctl pointer move -3000 -3000; wlrctl pointer move 600 400   # the desktop, forward
wlrctl pointer click left; sleep 0.5
wlrctl pointer move -3000 -3000; wlrctl pointer move 1240 114  # the disk icon
wlrctl pointer click left; sleep 0.5
$V cmd Y                                                        # Put Away
sleep 2
grim /tmp/net5.png

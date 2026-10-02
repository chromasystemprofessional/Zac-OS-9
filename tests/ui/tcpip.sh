#!/bin/sh
# UI test: the TCP/IP control panel against tests/ui/fake-nmcli.
# Run inside WSL:
#   rm -f /tmp/fake-nmcli.json /tmp/fake-nmcli.log
#   mkdir -p build/fakebin && cp tests/ui/fake-nmcli build/fakebin/nmcli
#   PATH="$PWD/build/fakebin:$PATH" scripts/snapshot.sh /tmp/tcp.png 50 "sh tests/ui/tcpip.sh"
#   python3 tests/ui/tcpip-check.py
# Expected:
#   /tmp/tcp-1.png  Ethernet, using DHCP: the server's address, mask and router
#   /tmp/tcp-2.png  the Connect via menu, Ethernet checked and level with the button
#   /tmp/tcp-3.png  Wi-Fi: a Network pop-up showing HomeNet
#   /tmp/tcp-4.png  the Network menu: signal bars, padlocks, Other Network…, Turn Wi-Fi Off
#   /tmp/tcp-5.png  the password dialog for "Neighbor", the password as dots
#   /tmp/tcp-6.png  joined: the Network pop-up shows Neighbor
#   /tmp/tcp-7.png  Configure: Manually, the fields filled from the server's values
#   /tmp/tcp-8.png  closing with changes: "Save changes to the current configuration?"
# tcpip-check.py checks the calls in /tmp/fake-nmcli.log.
# Clicks are relative to the panel's content, found in a screenshot.
V=build/shell/vptr
click_in() { # click_in X Y: at (X, Y) in the panel's content
	grim /tmp/tcp-where.png
	set -- $(python3 tests/ui/content-origin.py /tmp/tcp-where.png) "$1" "$2"
	$V home move $(($1 + $3)) $(($2 + $4)) click wait 800
}
sleep 2
build/shell/platinum-tcpip & sleep 4
grim /tmp/tcp-1.png
click_in 300 24                 # Connect via (a quick click: the menu stays open)
grim /tmp/tcp-2.png
$V move 0 16 click wait 1500    # Wi-Fi, the item below
grim /tmp/tcp-3.png
click_in 300 71                 # Network
grim /tmp/tcp-4.png
$V move 0 32 click wait 1500    # Neighbor, two items down: it needs a password
$V type "correct horse" wait 500
grim /tmp/tcp-5.png
$V key Return wait 3000         # Join
grim /tmp/tcp-6.png
click_in 300 97                 # Configure, showing "Using DHCP Server"
$V move 0 -16 click wait 1000   # Manually, the item above
grim /tmp/tcp-7.png
click_in 200 134                # the IP address field
$V cmd a type 192.168.1.50 wait 300
$V cmd w wait 1500              # close: the panel asks about the changes
grim /tmp/tcp-8.png
$V key Return wait 2000         # Save

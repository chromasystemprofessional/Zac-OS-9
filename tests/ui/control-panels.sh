#!/bin/sh
# UI test: Mouse, Keyboard, Sound and Monitors panels, and their effects.
# Run inside WSL:
#   rm -f ~/.config/platinum/desktop.conf
#   scripts/snapshot.sh /tmp/cp.png 34 "sh tests/ui/control-panels.sh"
#   python3 tests/ui/find-cursor.py /tmp/cp-slow.png /tmp/cp-fast.png
# Expected:
#   /tmp/cp-mouse.png     Mouse: tracking slider, double-click radios, test spot
#   /tmp/cp-slow.png      the pointer 100 px from the left edge (normal speed)
#   /tmp/cp-fast.png      after "Fast": the same move goes 220 px
#   /tmp/cp-keyboard.png  Keyboard: two sliders and a field to try them
#   /tmp/cp-sound.png     Sound: volume with Mute, alert volume
#   /tmp/cp-monitors.png  Monitors: resolutions and pixel size
#   /tmp/cp-large.png     after "Large (2x)": everything drawn at 2x
# Clicks are relative to the panel's content, found in a screenshot.
V=build/shell/vptr
P=build/shell/platinum-controlpanel
click_in() { # click_in X Y: at (X, Y) in the panel's content
	grim /tmp/cp-where.png
	set -- $(python3 tests/ui/content-origin.py /tmp/cp-where.png) "$1" "$2"
	$V home move $(($1 + $3)) $(($2 + $4)) click wait 800
}
sleep 3
$P mouse & sleep 3
grim /tmp/cp-mouse.png
$V home move 0 300 wait 200 move 100 0 wait 300
grim -c /tmp/cp-slow.png
click_in 327 47                                    # Mouse Tracking: Fast
$V home move 0 300 wait 200 move 100 0 wait 300
grim -c /tmp/cp-fast.png
rm -f ~/.config/platinum/desktop.conf; sleep 1
pkill -f "platinum-controlpanel mouse"; sleep 1
$P keyboard & sleep 3
grim /tmp/cp-keyboard.png
pkill -f "platinum-controlpanel keyboard"; sleep 1
$P sound & sleep 3
grim /tmp/cp-sound.png
pkill -f "platinum-controlpanel sound"; sleep 1
$P monitors & sleep 3
grim /tmp/cp-monitors.png
click_in 219 83                                    # Pixel size: Large (2x)
sleep 1
grim /tmp/cp-large.png
rm -f ~/.config/platinum/desktop.conf

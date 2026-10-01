#!/bin/sh
# UI test: the Appearance control panel changes the look live.
# Setup and run inside WSL:
#   rm -f ~/.config/platinum/desktop.conf
#   scripts/snapshot.sh /tmp/ap.png 14 "sh tests/ui/appearance-panel.sh"
# Expected:
#   /tmp/ap1.png  Appearance, Color tab: Accent Color and Highlight Color
#                 lists (Lavender selected), a Sample with scroll bar and
#                 progress bar
#   /tmp/ap2.png  Ocean picked: the sample, the list's focus ring and the
#                 File menu's highlight are all ocean blue
#   /tmp/ap3.png  Desktop tab, Ocean Ripple picked: preview and desktop
#   /tmp/ap4.png  Sound tab: the alert sounds, Platinum selected
# The window's content starts at (46, 42) on screen.
V=build/shell/vptr
build/shell/platinum-appearance &
sleep 4
grim /tmp/ap1.png
$V home move 110 194 click wait 600                  # Accent: Ocean
$V home move 50 9 click wait 500                     # File menu
grim /tmp/ap2.png
$V key Escape wait 300
$V home move 169 62 click wait 500                   # Desktop tab
$V home move 110 130 click wait 1200                 # Ocean Ripple
grim /tmp/ap3.png
$V home move 250 62 click wait 500                   # Sound tab
grim /tmp/ap4.png

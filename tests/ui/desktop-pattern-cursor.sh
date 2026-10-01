#!/bin/sh
# UI test: desktop patterns switch live, and the Platinum cursor shows.
# Setup and run inside WSL:
#   rm -f ~/.config/platinum/desktop.conf
#   scripts/snapshot.sh /tmp/pc.png 4 "sh tests/ui/desktop-pattern-cursor.sh"
# Expected:
#   /tmp/pc1.png  default "Platinum" pattern, Platinum arrow at (400,300)
#   /tmp/pc2.png  "Ocean Ripple" after desktop.conf changed, no restart
V=build/shell/vptr
sleep 3
$V home move 400 300 wait 300
grim -c /tmp/pc1.png
mkdir -p ~/.config/platinum
printf '[General]\npattern=ocean-ripple\n' > ~/.config/platinum/desktop.conf
sleep 1
grim -c /tmp/pc2.png
rm -f ~/.config/platinum/desktop.conf

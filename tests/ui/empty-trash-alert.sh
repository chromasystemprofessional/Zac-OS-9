#!/bin/sh
# UI test: Empty Trash asks first, in a Platinum alert.
# Setup and run inside WSL:
#   mkdir -p ~/.local/share/Trash/files && echo x > ~/.local/share/Trash/files/old.txt
#   scripts/snapshot.sh /tmp/alert.png 8 "sh tests/ui/empty-trash-alert.sh"
# Expected:
#   /tmp/alert1.png  a movable modal alert: caution icon, "The Trash contains
#                    1 item...", Cancel and a default OK button
#   /tmp/alert2.png  alert gone, Trash icon empty
sleep 3
wlrctl pointer move -3000 -3000
wlrctl pointer move 600 400
wlrctl pointer click left; sleep 0.5      # desktop: Finder in front
wlrctl pointer move -3000 -3000
wlrctl pointer move 190 9; sleep 0.2
wlrctl pointer click left; sleep 0.3      # Special
wlrctl pointer move 0 19; sleep 0.2
wlrctl pointer click left; sleep 1.5      # Empty Trash...
grim /tmp/alert1.png
wlrctl keyboard type "
"                                          # Return = default button (OK)
sleep 1
grim /tmp/alert2.png

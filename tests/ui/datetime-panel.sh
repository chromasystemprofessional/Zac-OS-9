#!/bin/sh
# UI test: the Date & Time control panel (changes are logged, not made).
# Run inside WSL:
#   rm -f ~/.config/platinum/desktop.conf
#   scripts/snapshot.sh /tmp/dt.png 16 "sh tests/ui/datetime-panel.sh"
#   grep 'would run' /tmp/datetime.log
# Expected:
#   /tmp/dt1.png  Date & Time: date and time clock controls, Time Zone with
#                 the current zone, Network Time, Menu Bar Clock options
#   /tmp/dt2.png  the hour selected (highlighted) and stepped up by one
#   /tmp/dt3.png  "Use 24-hour time" on: the panel and menu bar clocks in
#                 24-hour time
#   /tmp/dt4.png  the Set Time Zone dialog, its list at the current zone
#   /tmp/datetime.log  "would run: timedatectl set-time ..." for the hour
# The window's content starts at (46, 42) on screen.
V=build/shell/vptr
PLATINUM_DATETIME_DRYRUN=1 build/shell/platinum-datetime 2>/tmp/datetime.log &
sleep 4
grim /tmp/dt1.png
$V home move 84 162 click wait 300                  # the hour
$V home move 181 158 click wait 600                 # up arrow
grim /tmp/dt2.png
sleep 1.5                                           # the change is applied
$V home move 75 252 click wait 800                  # Use 24-hour time
grim /tmp/dt3.png
$V home move 328 114 click wait 1500                # Set Time Zone...
grim /tmp/dt4.png
$V key Escape wait 500

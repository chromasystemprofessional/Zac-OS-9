#!/bin/sh
# UI test: Get Info comments are editable and stored in user.xdg.comment.
# Setup and run inside WSL:
#   rm -rf ~/Desktop/* ~/.local/share/platinum/finder; echo hi > ~/Desktop/Report.txt
#   scripts/snapshot.sh /tmp/cm.png 8 "sh tests/ui/finder-comments.sh"
#   python3 -c "import os; print(os.getxattr(os.path.expanduser('~/Desktop/Report.txt'), 'user.xdg.comment'))"
# Expected: /tmp/cm1.png shows the comment typed in the Info window, and
# the attribute reads b'Quarterly numbers, draft 2'.
V=build/shell/vptr
sleep 3
$V home move 1240 114 click wait 700                     # select Report.txt
wlrctl pointer move -3000 -3000; wlrctl pointer move 50 9
wlrctl pointer click left; sleep 0.3
wlrctl pointer move 0 89; wlrctl pointer click left; sleep 1.2   # File > Get Info
# Info window: 300x280 client at the staggered spot; the comments box is
# near its bottom.
$V home move 150 300 click wait 300
wlrctl keyboard type "Quarterly numbers, draft 2"; sleep 0.8
grim /tmp/cm1.png

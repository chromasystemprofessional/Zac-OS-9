#!/bin/sh
# UI test: every kind of Finder icon, in a window in list view.
# Setup and run inside WSL:
#   rm -rf ~/Desktop/* ~/.local/share/platinum/finder
#   d=~/Desktop/Kinds; mkdir -p "$d/Folder" "$d/.finf"
#   echo hi > "$d/Notes.txt"; printf '[Desktop Entry]\nType=Application\nName=Tool\nExec=true\n' > "$d/Tool.desktop"
#   python3 -c "open('$d/System.dsk','wb').write(bytes(1024)+b'BD'+bytes(500000))"
#   echo x > "$d/TeachText"; printf 'APPLttxt' > "$d/.finf/TeachText"
#   scripts/snapshot.sh /tmp/kinds.png 6 "sh tests/ui/finder-kinds.sh"
# Expected: /tmp/kinds1.png  icon view: folder, document, application,
#   disk image and Classic icons; /tmp/kinds.png  the same in list view
#   with kinds "Macintosh disk image" and "classic application".
V=build/shell/vptr
sleep 3
$V home move 1240 114 click wait 100 click wait 1500       # open Kinds
grim /tmp/kinds1.png
wlrctl pointer move -3000 -3000; wlrctl pointer move 137 9
wlrctl pointer click left; sleep 0.3
wlrctl pointer move 0 51; wlrctl pointer click left; sleep 1   # View > as List

#!/bin/sh
# Start zacos9-wm nested, run some clients, save a screenshot, and exit.
# Run inside WSL Debian. Needs grim.
#
#   scripts/snapshot.sh out.png 3 'foot -T One' 'foot -T Two'
#
# Clients start 1.5 s apart (so they stagger like Finder windows); the shot
# is taken DELAY seconds after the last one starts.
set -eu
self=$(realpath "$0")
cd "$(dirname "$self")/.."
. scripts/x11-namespace.sh
out=$(realpath -m "$1")
delay=$2
shift 2
scripts/build.sh >/dev/null

startup=""
for client in "$@"; do
	startup="$startup $client & sleep 1.5;"
done
startup="$startup sleep $delay; grim '$out'; kill \$PPID"

export WLR_RENDERER="${WLR_RENDERER:-pixman}"
export WLR_WL_OUTPUTS=1
exec timeout 60 build/compositor/zacos9-wm -s "$startup" >/tmp/zacos9-snapshot.log 2>&1

#!/bin/sh
# Build, then run platinum-wm as a window inside the current Wayland session
# (WSLg on the Windows dev box). Extra args are passed to platinum-wm.
#
#   scripts/run-nested.sh                 # opens a terminal inside
#   scripts/run-nested.sh -s 'xterm'      # custom startup command
#
# Quit: Ctrl+Alt+Backspace inside the window, or close the window.
set -eu
cd "$(dirname "$0")/.."
scripts/build.sh

# WSLg exposes no reliable GPU path for nested wlroots; software render is
# plenty for a 1-bit-to-8-bit-era UI. Override with WLR_RENDERER=gles2.
export WLR_RENDERER="${WLR_RENDERER:-pixman}"
export WLR_WL_OUTPUTS=1

if [ $# -eq 0 ]; then
	set -- -s foot
fi
exec build/compositor/platinum-wm "$@"

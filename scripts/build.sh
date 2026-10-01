#!/bin/sh
# Configure (first run) and build everything. Run inside WSL Debian.
set -eu
cd "$(dirname "$0")/.."

if [ ! -f build/build.ninja ]; then
	meson setup build --buildtype=debug
fi
ninja -C build "$@"

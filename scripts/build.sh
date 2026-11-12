#!/bin/sh
# Configure (first run) and build the applications. Test binaries are built
# by meson test on demand (docs/build-speed.md).
set -eu
cd "$(dirname "$0")/.."

if [ ! -f build/build.ninja ]; then
	meson setup build --buildtype=debug
fi
ninja -C build "$@"

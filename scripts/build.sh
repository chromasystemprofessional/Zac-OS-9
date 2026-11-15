#!/bin/sh
# Configure (first run) and build the applications. Test binaries are built
# by meson test on demand (docs/build-speed.md).
set -eu
cd "$(dirname "$0")/.."

for tool in meson ninja; do
	if ! command -v "$tool" >/dev/null; then
		echo "build.sh: $tool is missing. Install the build dependencies on Debian 13" >&2
		echo "(sudo apt build-dep ./), or run this in a Debian 13 machine or WSL 2." >&2
		echo "CONTRIBUTING.md has the steps." >&2
		exit 1
	fi
done

if [ ! -f build/build.ninja ]; then
	meson setup build --buildtype=debug
fi
ninja -C build "$@"

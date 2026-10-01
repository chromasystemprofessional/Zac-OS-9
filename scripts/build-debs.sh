#!/bin/sh
# Build the Debian packages into build/packages. Run inside Debian 13
# (WSL Debian on the Windows dev box); installs missing build tools.
#
#   scripts/build-debs.sh               # platinum-2026
#   scripts/build-debs.sh --emulators   # and platinum-emulators
#
# The package is built from what `git add -A` would commit: tracked and
# new files, without anything .gitignore excludes. Build output and the
# Apple files in macos/ and uploads/ never get into a package.
set -eu
self=$(realpath "$0")
cd "$(dirname "$self")/.."
root=$(pwd)
out=$root/build/packages
mkdir -p "$out"
rm -f "$out"/platinum-2026*.deb

sudo_() { if [ "$(id -u)" = 0 ]; then "$@"; else sudo "$@"; fi; }
if ! command -v dpkg-buildpackage >/dev/null || ! command -v dh >/dev/null; then
	sudo_ apt-get install -y --no-install-recommends build-essential debhelper devscripts fakeroot
fi

work=$(mktemp -d /tmp/platinum-deb.XXXXXX)
trap 'rm -rf "$work"' EXIT
index=$work/index
GIT_INDEX_FILE=$index git read-tree HEAD
GIT_INDEX_FILE=$index git add -A
tree=$(GIT_INDEX_FILE=$index git write-tree)
mkdir "$work/platinum-2026"
git archive "$tree" | tar -x -C "$work/platinum-2026"

cd "$work/platinum-2026"
if ! dpkg-checkbuilddeps 2>/dev/null; then
	sudo_ apt-get build-dep -y ./
fi
dpkg-buildpackage -b -us -uc -j"$(nproc)"
cp "$work"/*.deb "$out/"

if [ "${1:-}" = --emulators ]; then
	sh "$root/packaging/emulators/build-deb.sh"
fi
ls -l "$out"

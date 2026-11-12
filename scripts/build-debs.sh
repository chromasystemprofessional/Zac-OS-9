#!/bin/sh
# Build the Debian packages into build/packages. Run inside Debian 13
# (WSL Debian on the Windows dev box) as your normal user; it asks for sudo
# to install missing build tools. The package tests assume a normal user's
# permissions.
#
#   scripts/build-debs.sh               # zacos9
#   scripts/build-debs.sh --emulators   # and zacos9-emulators
#
# The package is built from what `git add -A` would commit: tracked and
# new files, without anything .gitignore excludes. Build output and the
# Apple files in macos/ and uploads/ never get into a package.
set -eu
self=$(realpath "$0")
cd "$(dirname "$self")/.."
root=$(pwd)

# A root build runs the package tests as root, where a different one fails
# every release. Re-run as the user who invoked sudo.
if [ "$(id -u)" = 0 ]; then
	if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ]; then
		echo "build-debs.sh: running as $SUDO_USER; the package tests need a normal user." >&2
		exec su -s /bin/sh "$SUDO_USER" -c 'cd "$1" && shift && exec sh "$@"' \
			sh "$root" "$self" "$@"
	fi
	echo "build-debs.sh: run it as your normal user, not as root." >&2
	exit 1
fi
# rustup installs cargo outside PATH. The tests and dpkg-checkbuilddeps need
# it even after su has reset PATH for the user who ran sudo.
if [ -d "$HOME/.cargo/bin" ]; then
	PATH="$HOME/.cargo/bin:$PATH"
	export PATH
fi
out=$root/build/packages
mkdir -p "$out"
rm -f "$out"/zacos9*.deb

sudo_() { if [ "$(id -u)" = 0 ]; then "$@"; else sudo "$@"; fi; }
if ! command -v dpkg-buildpackage >/dev/null || ! command -v dh >/dev/null; then
	sudo_ apt-get install -y --no-install-recommends build-essential debhelper devscripts fakeroot
fi

work=$(mktemp -d /tmp/zacos9-deb.XXXXXX)
trap 'rm -rf "$work"' EXIT
index=$work/index
GIT_INDEX_FILE=$index git read-tree HEAD
GIT_INDEX_FILE=$index git add -A
tree=$(GIT_INDEX_FILE=$index git write-tree)
mkdir "$work/zacos9"
git archive "$tree" | tar -x -C "$work/zacos9"

cd "$work/zacos9"
if ! dpkg-checkbuilddeps 2>/dev/null; then
	sudo_ apt-get build-dep -y ./
fi
# The package tests include the desktop's own UI tests. They use a private
# compositor, but some need an X display as well; CI runs them this way.
if command -v xvfb-run >/dev/null 2>&1; then
	xvfb-run -a dpkg-buildpackage -b -us -uc -j"$(nproc)"
else
	dpkg-buildpackage -b -us -uc -j"$(nproc)"
fi
cp "$work"/*.deb "$out/"

if [ "${1:-}" = --emulators ]; then
	sh "$root/packaging/emulators/build-deb.sh"
fi
ls -l "$out"

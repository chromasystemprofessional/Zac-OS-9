#!/bin/sh
# Build the zacos9 package from the working tree and install it on this
# computer, for trying unreleased changes in the real session. Asks for the
# admin password once, at the install. Log out and back in afterwards: the
# compositor is the session, so its changes need a new one.
#
#   scripts/install-live.sh              # build, then install
#   scripts/install-live.sh --no-build   # install what build/packages has
#
# Releases reach other computers through Software Update instead.
set -eu
self=$(realpath "$0")
cd "$(dirname "$self")/.."

if [ "${1:-}" != --no-build ]; then
	scripts/build-debs.sh
fi

deb=$(ls build/packages/zacos9_*.deb 2>/dev/null | head -n 1)
if [ -z "$deb" ]; then
	echo "install-live: no zacos9 package in build/packages; run without --no-build" >&2
	exit 1
fi
# apt, not dpkg: it also installs dependencies the package has gained.
# --reinstall: the working tree usually has the installed version number.
sudo apt-get install -y --reinstall "./$deb"

echo
echo "Installed $(basename "$deb"). Log out and back in to start the new session."

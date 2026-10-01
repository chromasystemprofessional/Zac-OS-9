#!/bin/sh
# Build SheepShaver (PowerPC, Mac OS 7.5.2 to 9.0.4) and Basilisk II
# (68k, System 7 to Mac OS 8.1) from the maintained macemu fork.
# Binaries land in build/emulators/bin. Apple ROMs and system disks are
# never part of this: the user supplies them (see emulation/README.md).
#
#   sh emulation/build-emulators.sh
set -eu

MACEMU_REPO=https://github.com/kanjitalk755/macemu.git
MACEMU_REF=${MACEMU_REF:-master}

root=$(cd "$(dirname "$0")/.." && pwd)
src=$root/build/emulators/macemu
out=$root/build/emulators/bin
jobs=$(nproc)

apt-get install -y --no-install-recommends \
	git autoconf automake libtool pkg-config g++ make \
	libsdl2-dev libgtk-3-dev libmpfr-dev >/dev/null

if [ ! -d "$src/.git" ]; then
	git clone --depth 1 --branch "$MACEMU_REF" "$MACEMU_REPO" "$src"
fi
mkdir -p "$out"

# SheepShaver: SDL video and audio, no GTK preferences editor (our own
# setup assistant writes the prefs file).
cd "$src/SheepShaver"
make links >/dev/null
cd src/Unix
NO_CONFIGURE=1 ./autogen.sh >/dev/null
./configure --enable-sdl-video --enable-sdl-audio --without-gtk \
	--disable-vosf >/dev/null
make -j"$jobs" >/dev/null
cp SheepShaver "$out/"

# Basilisk II.
cd "$src/BasiliskII/src/Unix"
NO_CONFIGURE=1 ./autogen.sh >/dev/null
./configure --enable-sdl-video --enable-sdl-audio --without-gtk \
	--disable-vosf --enable-jit-compiler >/dev/null
make -j"$jobs" >/dev/null
cp BasiliskII "$out/"

echo "built: $(ls "$out")"

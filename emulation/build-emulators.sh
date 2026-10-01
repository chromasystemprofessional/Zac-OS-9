#!/bin/sh
# Build SheepShaver (PowerPC, Mac OS 7.5.2 to 9.0.4) and Basilisk II
# (68k, System 7 to Mac OS 8.1) from the maintained macemu fork.
# Binaries land in build/emulators/bin. Apple ROMs and system disks are
# never part of this: the user supplies them (see emulation/README.md).
#
#   sh emulation/build-emulators.sh
set -eu

MACEMU_REPO=https://github.com/kanjitalk755/macemu.git
# The commit we test against (and package); override to try another.
MACEMU_COMMIT=${MACEMU_COMMIT:-892eeb74ab9d70dfb034138a0b39057b14f275bc}

root=$(cd "$(dirname "$0")/.." && pwd)
src=$root/build/emulators/macemu
out=$root/build/emulators/bin
jobs=$(nproc)

sudo_() { if [ "$(id -u)" = 0 ]; then "$@"; else sudo "$@"; fi; }
sudo_ apt-get install -y --no-install-recommends \
	git autoconf automake libtool pkg-config g++ make \
	libsdl2-dev libgtk-3-dev libmpfr-dev >/dev/null

if [ ! -d "$src/.git" ]; then
	git init -q "$src"
	git -C "$src" remote add origin "$MACEMU_REPO"
fi
if [ "$(git -C "$src" rev-parse HEAD 2>/dev/null)" != "$MACEMU_COMMIT" ]; then
	git -C "$src" fetch -q --depth 1 origin "$MACEMU_COMMIT"
	git -C "$src" checkout -q --force FETCH_HEAD
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

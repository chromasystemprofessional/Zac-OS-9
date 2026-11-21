#!/bin/sh
# Build SheepShaver (PowerPC, Mac OS 7.5.2 to 9.0.4) and Basilisk II
# (68k, System 7 to Mac OS 8.1) from the maintained macemu fork.
# SheepShaver gets the Classic bridge patch from emulation/patches.
# In a source checkout the binaries land in build/emulators/bin. A copy
# downloaded on its own fetches the pinned source and patch and builds into
# ~/.local/libexec/zacos9, where zacos9-classic finds them. Apple ROMs and
# system disks are never part of this: the user supplies them (see
# emulation/README.md).
#
#   sh emulation/build-emulators.sh
set -eu

MACEMU_REPO=https://github.com/kanjitalk755/macemu.git
# The commit we test against (and package); override to try another.
MACEMU_COMMIT=${MACEMU_COMMIT:-892eeb74ab9d70dfb034138a0b39057b14f275bc}
# A standalone copy has no emulation/patches; take the same patch from main.
PATCH_URL=${ZACOS9_PATCH_URL:-https://raw.githubusercontent.com/chromasystemprofessional/Zac-OS-9/main/emulation/patches/sheepshaver-classic-bridge.patch}

self=$(realpath "$0")
script_dir=$(dirname "$self")
jobs=$(nproc)

# The script installs its build tools through sudo by itself; running the
# whole build as root would leave root-owned files in the user's cache.
if [ "$(id -u)" = 0 ]; then
	echo "build-emulators.sh: run it as your normal user, without sudo;" >&2
	echo "it asks for sudo itself to install the build tools." >&2
	exit 1
fi

# A checkout keeps its build beside the tree. A standalone copy writes to
# the user's libexec, one of the places zacos9-classic searches, so the
# desktop finds the emulators without an environment variable.
if [ -f "$script_dir/../meson.build" ] && [ -d "$script_dir/../emulation/patches" ]; then
	root=$(cd "$script_dir/.." && pwd)
	src=$root/build/emulators/macemu
	out=$root/build/emulators/bin
	patch=$root/emulation/patches/sheepshaver-classic-bridge.patch
	tree=yes
else
	src=${XDG_CACHE_HOME:-$HOME/.cache}/zacos9/emulators/macemu
	out=$HOME/.local/libexec/zacos9
	patch=${XDG_CACHE_HOME:-$HOME/.cache}/zacos9/emulators/sheepshaver-classic-bridge.patch
	tree=
fi

sudo_() { if [ "$(id -u)" = 0 ]; then "$@"; else sudo "$@"; fi; }
sudo_ apt-get install -y --no-install-recommends \
	git autoconf automake libtool pkg-config g++ make curl ca-certificates \
	libcap2-bin libsdl2-dev libgtk-3-dev libmpfr-dev >/dev/null

if [ ! -d "$src/.git" ]; then
	mkdir -p "$(dirname "$src")"
	git init -q "$src"
	git -C "$src" remote add origin "$MACEMU_REPO"
fi
if [ "$(git -C "$src" rev-parse HEAD 2>/dev/null)" != "$MACEMU_COMMIT" ]; then
	git -C "$src" fetch -q --depth 1 origin "$MACEMU_COMMIT"
	git -C "$src" checkout -q --force FETCH_HEAD
fi

# Reset every file the patch touches before applying it again. Resetting only
# the two SheepShaver files left the other hunks applied, so a second run
# failed with "patch does not apply".
git -C "$src" checkout -q -- .
if [ ! -f "$patch" ]; then
	if [ -n "$tree" ]; then
		echo "build-emulators.sh: $patch is missing." >&2
		exit 1
	fi
	mkdir -p "$(dirname "$patch")"
	if ! curl -fsSL "$PATCH_URL" -o "$patch"; then
		rm -f "$patch"
		echo "build-emulators.sh: could not fetch the Classic bridge patch;" \
			"building without it (direct launching will not be available)." >&2
	fi
fi
if [ -f "$patch" ] && ! git -C "$src" apply "$patch"; then
	if [ -n "$tree" ]; then
		echo "build-emulators.sh: could not apply $patch." >&2
		exit 1
	fi
	echo "build-emulators.sh: the fetched Classic bridge patch does not apply;" \
		"building without it (direct launching will not be available)." >&2
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
if [ -z "$tree" ]; then
	echo "zacos9-classic finds them in $out."
	# The packages grant this to /usr/libexec/zacos9/SheepShaver; a
	# user-local build needs the same permission for an Old World ROM.
	if sudo setcap cap_sys_rawio=ep "$out/SheepShaver" 2>/dev/null; then
		echo "Gave SheepShaver the low-memory permission for Old World ROMs."
	else
		echo "Could not give SheepShaver the low-memory permission;" \
			"an Old World ROM may not start." >&2
	fi
	echo "On a ZacOS 9 older than this script, start Old Dog with:"
	echo "  ZACOS9_EMULATORS=$out zacos9-classic"
fi

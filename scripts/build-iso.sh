#!/bin/sh
# Build the ZacOS 9 live and install ISO into build/iso. Run
# inside Debian 13 (WSL Debian on the Windows dev box) with sudo; needs
# a network connection and about 10 GB free. The first build takes
# 20-40 minutes; later ones reuse the downloaded packages.
#
#   scripts/build-iso.sh
#
# The ISO boots straight into ZacOS 9 as the user "user" (no
# password). Its boot menu also offers Debian's installer, which copies
# the system to a disk; the installed system starts at a login screen.
set -eu
self=$(realpath "$0")
cd "$(dirname "$self")/.."
root=$(pwd)

sudo_() { if [ "$(id -u)" = 0 ]; then "$@"; else sudo "$@"; fi; }
if ! command -v lb >/dev/null; then
	sudo_ apt-get install -y --no-install-recommends live-build debootstrap \
		squashfs-tools xorriso
fi

# Our packages first: the ISO installs them from build/packages.
sh scripts/build-debs.sh --emulators

# live-build makes a chroot here, so it must be a Linux file system
# (not /mnt/c or /mnt/d).
work=${ZACOS9_ISO_WORK:-/var/tmp/zacos9-iso}
sudo_ mkdir -p "$work"
cd "$work"
sudo_ lb clean >/dev/null 2>&1 || true
sudo_ rm -rf config
sudo_ lb config \
	--distribution trixie \
	--archive-areas "main contrib non-free non-free-firmware" \
	--firmware-chroot true \
	--debian-installer live \
	--iso-application "ZacOS 9" \
	--iso-publisher "ZacOS 9" \
	--iso-volume "ZacOS 9" \
	--image-name zacos9 \
	--bootappend-live "$(cat "$root/iso/kernel-params")" \
	--memtest none

sudo_ cp -r "$root/iso/config/." config/
sudo_ mkdir -p config/packages.chroot
sudo_ cp "$root"/build/packages/zacos9_*.deb "$root"/build/packages/zacos9-emulators_*.deb \
	config/packages.chroot/
# Copies from a Windows drive come out as mode 777: set real modes.
sudo_ find config/includes.chroot config/package-lists config/packages.chroot \
	config/bootloaders -type f \
	-exec chmod 644 {} +
sudo_ chmod 755 config/includes.chroot/usr/lib/live/config/2000-zacos9-greetd \
	config/hooks/normal/9000-zacos9.hook.chroot

sudo_ lb build
version=$(dpkg-parsechangelog -l "$root/debian/changelog" -S Version)
mkdir -p "$root/build/iso"
cp zacos9-amd64.hybrid.iso "$root/build/iso/zacos9-$version-amd64.iso"
ls -l "$root/build/iso"

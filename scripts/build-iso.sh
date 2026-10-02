#!/bin/sh
# Build the Platinum 2026 live and install ISO into build/iso. Run
# inside Debian 13 (WSL Debian on the Windows dev box) with sudo; needs
# a network connection and about 10 GB free. The first build takes
# 20-40 minutes; later ones reuse the downloaded packages.
#
#   scripts/build-iso.sh
#
# The ISO boots straight into Platinum 2026 as the user "user" (no
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
work=${PLATINUM_ISO_WORK:-/var/tmp/platinum-iso}
sudo_ mkdir -p "$work"
cd "$work"
sudo_ lb clean >/dev/null 2>&1 || true
sudo_ rm -rf config
sudo_ lb config \
	--distribution trixie \
	--archive-areas "main contrib non-free non-free-firmware" \
	--firmware-chroot true \
	--debian-installer live \
	--iso-application "Platinum 2026" \
	--iso-publisher "Platinum 2026" \
	--iso-volume "Platinum 2026" \
	--image-name platinum-2026 \
	--bootappend-live "boot=live components quiet splash hostname=platinum username=user" \
	--memtest none

sudo_ cp -r "$root/iso/config/." config/
sudo_ mkdir -p config/packages.chroot
sudo_ cp "$root"/build/packages/platinum-2026_*.deb "$root"/build/packages/platinum-emulators_*.deb \
	config/packages.chroot/
# Copies from a Windows drive come out as mode 777: set real modes.
sudo_ find config/includes.chroot config/package-lists config/packages.chroot \
	config/bootloaders -type f \
	-exec chmod 644 {} +
sudo_ chmod 755 config/includes.chroot/usr/lib/live/config/2000-platinum-greetd \
	config/hooks/normal/9000-platinum.hook.chroot

sudo_ lb build
version=$(dpkg-parsechangelog -l "$root/debian/changelog" -S Version)
mkdir -p "$root/build/iso"
cp platinum-2026-amd64.hybrid.iso "$root/build/iso/platinum-2026-$version-amd64.iso"
ls -l "$root/build/iso"

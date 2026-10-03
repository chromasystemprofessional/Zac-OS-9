#!/bin/sh
# Try changes to the system and the way it boots in a VM, without building
# the ISO. Takes the system from the last ISO build (its chroot in
# /var/tmp/zacos9-iso), puts this tree's packages, iso/config and its
# hooks on top (in an overlay, so the build's copy stays as it was), packs
# it (a minute or two) and boots it in QEMU with the kernel and initramfs
# given directly. The boot menu (GRUB, ISOLINUX) is the one part this
# doesn't show; that needs the ISO.
#
#   scripts/dev-boot.sh              # build packages, update, boot in a window
#   scripts/dev-boot.sh --no-debs    # without rebuilding the packages
#   scripts/dev-boot.sh --record     # no window: the screen every 0.2 s for
#                                    # 90 s in /tmp/devboot, and a timeline
#   scripts/dev-boot.sh --keep       # with --record: leave the VM running after
#   scripts/dev-boot.sh --retina     # a 2560 x 1600 screen
#   scripts/dev-boot.sh --clean      # start over from the ISO build's system
#
# A login runs on the VM's serial port (user / live):
#   python3 tests/vm/serial.py ~/.local/share/zacos9-vm/serial.sock 'command'
# Files iso/config no longer has stay in the overlay until --clean.
set -eu
self=$(realpath "$0")
cd "$(dirname "$self")/.."
root=$(pwd)

sudo_() { if [ "$(id -u)" = 0 ]; then "$@"; else sudo "$@"; fi; }
base=${ZACOS9_ISO_WORK:-/var/tmp/zacos9-iso}/chroot
dev=/var/tmp/zacos9-dev
vm=${ZACOS9_VM_DIR:-$HOME/.local/share/zacos9-vm}
if [ ! -d "$base/usr" ]; then
	echo "No ISO build to start from: run scripts/build-iso.sh once." >&2
	exit 2
fi

debs=true record=false clean=false keep=false gpu=virtio-vga
for arg in "$@"; do
	case $arg in
	--no-debs) debs=false ;;
	--record) record=true ;;
	--clean) clean=true ;;
	--keep) keep=true ;;
	--retina) gpu=virtio-vga,xres=2560,yres=1600 ;;
	*) echo "usage: dev-boot.sh [--no-debs] [--record] [--retina] [--clean]" >&2; exit 64 ;;
	esac
done

if $debs; then
	sh scripts/build-debs.sh >/dev/null
fi
if $clean; then
	sudo_ rm -rf "$dev"
fi

# ---- the system: the ISO build's, with this tree on top ----
r=$dev/root
sudo_ mkdir -p "$dev/upper" "$dev/work" "$r" "$dev/medium/live" "$vm"
mounted=""
cleanup() {
	for m in $mounted; do
		sudo_ umount "$m" 2>/dev/null || true
	done
}
trap cleanup EXIT
sudo_ mount -t overlay overlay -o "lowerdir=$base,upperdir=$dev/upper,workdir=$dev/work" "$r"
mounted="$r"

# iso/config's files, with real modes (a Windows drive makes them 777).
sudo_ cp -r "$root/iso/config/includes.chroot/." "$r/"
(cd "$root/iso/config/includes.chroot" && find . -type f) | while IFS= read -r f; do
	sudo_ chmod 644 "$r/$f"
done
sudo_ chmod 755 "$r/usr/lib/live/config/2000-zacos9-greetd"
sudo_ mkdir -p "$r/tmp/dev-boot"
sudo_ cp "$root"/build/packages/zacos9_*.deb "$root"/iso/config/hooks/normal/*.hook.chroot \
	"$r/tmp/dev-boot/"
packages=$(cat "$root"/iso/config/package-lists/*.list.chroot | sed 's/#.*//' |
	grep -v '^zacos9' | tr '\n' ' ')

# Name resolution for apt inside; the image's own resolv.conf (a symlink)
# goes back afterwards.
sudo_ rm -f "$dev/resolv.conf.saved"
sudo_ cp -P "$r/etc/resolv.conf" "$dev/resolv.conf.saved" 2>/dev/null || true
sudo_ rm -f "$r/etc/resolv.conf"
sudo_ cp /etc/resolv.conf "$r/etc/resolv.conf"
for m in proc sys dev dev/pts run; do
	sudo_ mount --bind "/$m" "$r/$m"
	mounted="$r/$m $mounted"
done
sudo_ chroot "$r" /bin/sh -c "
	set -e
	export DEBIAN_FRONTEND=noninteractive
	missing=''
	for p in $packages; do dpkg -s \$p >/dev/null 2>&1 || missing=\"\$missing \$p\"; done
	if [ -n \"\$missing\" ]; then
		echo \"installing:\$missing\"
		apt-get update -qq
		# Keep iso/config's versions of files the packages also bring.
		apt-get install -y -qq -o Dpkg::Options::=--force-confold \$missing </dev/null >/dev/null
	fi
	dpkg -i --force-confold /tmp/dev-boot/*.deb </dev/null >/dev/null
	for h in /tmp/dev-boot/*.hook.chroot; do sh \$h; done
	# Dev boots only: a login on the serial port, for tests/vm/serial.py.
	systemctl enable serial-getty@ttyS0.service >/dev/null 2>&1
	rm -rf /tmp/dev-boot
"
for m in dev/pts dev proc sys run; do
	sudo_ umount "$r/$m"
done
mounted="$r"
sudo_ rm -f "$r/etc/resolv.conf"
if [ -e "$dev/resolv.conf.saved" ] || [ -L "$dev/resolv.conf.saved" ]; then
	sudo_ cp -P "$dev/resolv.conf.saved" "$r/etc/resolv.conf"
fi

# ---- pack it: a data disc with live/filesystem.squashfs, which live-boot finds ----
sudo_ cp "$(ls "$r"/boot/vmlinuz-* | tail -1)" "$dev/vmlinuz"
sudo_ cp "$(ls "$r"/boot/initrd.img-* | tail -1)" "$dev/initrd.img"
sudo_ mksquashfs "$r" "$dev/medium/live/filesystem.squashfs" -noappend -comp lz4 -quiet
cleanup
mounted=""
sudo_ xorriso -as mkisofs -quiet -r -V "ZacOS 9" -o "$dev/medium.iso" "$dev/medium"

# ---- boot it ----
set -- -machine q35 -m 4096 -smp 4 -device "$gpu" \
	-device qemu-xhci -device usb-tablet -device usb-kbd \
	-nic user,model=virtio-net-pci -cdrom "$dev/medium.iso" \
	-kernel "$dev/vmlinuz" -initrd "$dev/initrd.img" -append "$(cat "$root/iso/kernel-params")" \
	-monitor "unix:$vm/monitor.sock,server,nowait" -serial "unix:$vm/serial.sock,server,nowait" \
	-name "ZacOS 9 (dev boot)"
[ -w /dev/kvm ] && set -- "$@" -enable-kvm -cpu host
if ! $record; then
	exec qemu-system-x86_64 "$@" -display gtk,zoom-to-fit=off
fi

rm -rf /tmp/devboot
mkdir -p /tmp/devboot
rm -f "$vm/monitor.sock" "$vm/serial.sock"
qemu-system-x86_64 "$@" -display none &
qemu=$!
trap 'kill $qemu 2>/dev/null || true' EXIT
while [ ! -S "$vm/monitor.sock" ]; do sleep 0.1; done
python3 "$root/tests/vm/record.py" "$vm/monitor.sock" /tmp/devboot 90 0.2
python3 "$root/tests/vm/boot-frames.py" /tmp/devboot
if $keep; then
	echo "The VM keeps running (serial: $vm/serial.sock)."
	wait $qemu
fi

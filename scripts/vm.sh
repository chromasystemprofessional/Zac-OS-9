#!/bin/sh
# Run ZacOS 9 in a QEMU virtual machine: the whole system from
# the ISO, on a (virtual) screen of its own rather than nested in a
# window. Run inside WSL Debian or any Linux with QEMU; uses KVM when
# /dev/kvm is there.
#
#   scripts/vm.sh               # boot the newest ISO in build/iso (live)
#   scripts/vm.sh --install     # ISO plus a 20 GB disk, to try the installer
#   scripts/vm.sh --disk        # boot the installed disk
#   scripts/vm.sh --uefi ...    # boot with UEFI firmware instead of BIOS
#   scripts/vm.sh --headless    # no window (for tests)
#
# The disk and QEMU's monitor socket (for screendump, sendkey) are in
# ~/.local/share/zacos9-vm (in WSL, a Windows drive is too slow for a
# disk and can't hold sockets); delete disk.qcow2 to start over. The mouse
# is a tablet, so the pointer follows the host's without grabbing.
set -eu
self=$(realpath "$0")
cd "$(dirname "$self")/.."

# qemu-system-gui has the window and the sound output.
if ! command -v qemu-system-x86_64 >/dev/null || ! dpkg -s qemu-system-gui >/dev/null 2>&1; then
	sudo apt-get install -y --no-install-recommends qemu-system-x86 qemu-system-gui 		qemu-utils ovmf
fi

boot=cdrom
uefi=false
display="-display gtk,zoom-to-fit=off"
while [ $# -gt 0 ]; do
	case $1 in
	--install) boot=install ;;
	--disk) boot=disk ;;
	--uefi) uefi=true ;;
	--headless) display="-display none" ;;
	*) echo "usage: vm.sh [--install|--disk] [--uefi] [--headless]" >&2; exit 64 ;;
	esac
	shift
done

vm=${ZACOS9_VM_DIR:-$HOME/.local/share/zacos9-vm}
mkdir -p "$vm"
disk=$vm/disk.qcow2
set -- -machine q35 -m 4096 -smp 4 \
	-device virtio-vga \
	-device qemu-xhci -device usb-tablet -device usb-kbd \
	-nic user,model=virtio-net-pci \
	-monitor unix:"$vm"/monitor.sock,server,nowait \
	-name "ZacOS 9"
[ -w /dev/kvm ] && set -- "$@" -enable-kvm -cpu host
# Sound through the host's PulseAudio (WSLg has one), if there is one.
if [ -n "${PULSE_SERVER:-}" ] || [ -S "${XDG_RUNTIME_DIR:-/nonexistent}/pulse/native" ]; then
	set -- "$@" -audiodev pa,id=snd -device intel-hda -device hda-duplex,audiodev=snd
fi
if $uefi; then
	cp -n /usr/share/OVMF/OVMF_VARS_4M.fd "$vm"/efivars.fd
	set -- "$@" -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
		-drive if=pflash,format=raw,file="$vm"/efivars.fd
fi

if [ "$boot" != disk ]; then
	iso=$(ls -t build/iso/*.iso 2>/dev/null | head -1)
	[ -n "$iso" ] || { echo "No ISO in build/iso: run scripts/build-iso.sh first." >&2; exit 2; }
	set -- "$@" -cdrom "$iso" -boot order=dc
fi
if [ "$boot" != cdrom ]; then
	[ -f "$disk" ] || qemu-img create -q -f qcow2 "$disk" 20G
	set -- "$@" -drive file="$disk",if=virtio,format=qcow2
fi
# shellcheck disable=SC2086
exec qemu-system-x86_64 "$@" $display

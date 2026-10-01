#!/bin/sh
# VM test: the ISO boots, logs in by itself and reaches the Platinum
# desktop. Run inside WSL Debian after scripts/build-iso.sh:
#   sh tests/vm/boot.sh [--uefi]
# Expected: "desktop up after N s"; /tmp/vm-desktop.png shows the menu
# bar and the Finder's desktop. On failure /tmp/vm-last.png shows where
# it stopped.
set -eu
self=$(realpath "$0")
cd "$(dirname "$self")/../.."
vm=${PLATINUM_VM_DIR:-$HOME/.local/share/platinum-vm}
mon() { python3 tests/vm/monitor.py "$vm/monitor.sock" "$@"; }

rm -f "$vm/monitor.sock"
sh scripts/vm.sh --headless "$@" &
qemu=$!
trap 'kill $qemu 2>/dev/null || true' EXIT
while [ ! -S "$vm/monitor.sock" ]; do sleep 1; done

start=$(date +%s) # the boot menu starts the live system by itself
while :; do
	sleep 5
	elapsed=$(($(date +%s) - start))
	mon "screendump /tmp/vm-last.png -f png" >/dev/null
	if python3 tests/vm/desktop-up.py /tmp/vm-last.png; then
		sleep 5 # let the Finder finish drawing
		mon "screendump /tmp/vm-desktop.png -f png" >/dev/null
		echo "desktop up after $elapsed s"
		exit 0
	fi
	if [ "$elapsed" -gt 300 ]; then
		echo "no desktop after $elapsed s; see /tmp/vm-last.png" >&2
		exit 1
	fi
done

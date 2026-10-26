#!/bin/sh
# boot/31_zacos9_haiku (Haiku in GRUB's menu, issue #42) against stand-in
# blkid, mount, umount and grub-probe: on BIOS Haiku's boot sector is
# chainloaded, on UEFI only Haiku's own EFI loader is, nothing is offered
# unless other systems are looked for, and what it prints is a grub.cfg
# that GRUB's own checker accepts, even when mount warns on stdout (as
# util-linux's does: that once made update-grub refuse the whole file).
#
#   test_grub_haiku.sh 31_ZACOS9_HAIKU
set -u
script=$(realpath "$1")
if ! command -v grub-script-check >/dev/null || [ ! -f /usr/share/grub/grub-mkconfig_lib ]; then
	echo "SKIP: grub-common is needed"
	exit 77
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
bin=$work/bin
mkdir -p "$bin" "$work/disks/esp-haiku/EFI/BOOT" "$work/disks/esp-zacos/EFI/BOOT" "$work/efi"
printf 'MZ...Welcome to the Haiku boot loader!...' >"$work/disks/esp-haiku/EFI/BOOT/BOOTX64.EFI"
printf 'MZ...shim...' >"$work/disks/esp-zacos/EFI/BOOT/BOOTX64.EFI"
failures=0

cat >"$bin/blkid" <<'STUB'
#!/bin/sh
case "$*" in
*TYPE=befs*) echo /dev/haiku ;;
*TYPE=vfat*) echo /dev/esp-haiku; echo /dev/esp-zacos ;;
esac
STUB
cat >"$bin/mount" <<'STUB'
#!/bin/sh
# mount -o ro DEVICE DIR: the "disk"'s files, and a warning on stdout.
echo "mount warning: * ${3#/dev/}: Can't mount, would change RO state"
cp -r "$DISKS/${3#/dev/}/." "$4/"
STUB
cat >"$bin/umount" <<'STUB'
#!/bin/sh
rm -rf "${1:?}"/*
STUB
cat >"$bin/grub-probe" <<'STUB'
#!/bin/sh
case "$*" in
*fs_uuid*) echo 1234-ABCD ;;
*hints_string* | *abstraction*) ;;
*partmap*) echo gpt ;;
*-t\ fs* | *--target=fs*) echo fat ;;
*drive*) echo "(hd0,gpt1)" ;;
esac
STUB
chmod +x "$bin"/*

run() { # firmware dir, GRUB_DISABLE_OS_PROBER
	env PATH="$bin:/usr/bin:/bin" DISKS="$work/disks" grub_probe="$bin/grub-probe" \
		ZACOS9_FIRMWARE_EFI="$1" GRUB_DISABLE_OS_PROBER="$2" sh "$script" >"$work/out" 2>"$work/err"
}
check() {
	if eval "$1"; then
		echo "ok: $2"
	else
		echo "FAIL: $2"
		failures=$((failures + 1))
		sed 's/^/    /' "$work/out" "$work/err"
	fi
}
valid() { grub-script-check "$work/out"; }

run "$work/no-efi" false
check 'valid && grep -q "^menuentry .Haiku." "$work/out" && grep -q "chainloader +1" "$work/out"' \
	"BIOS: Haiku's boot sector, chainloaded"
run "$work/efi" false
check 'valid && [ "$(grep -c "^menuentry" "$work/out")" = 1 ] && grep -q "chainloader /EFI/BOOT/BOOTX64.EFI" "$work/out"' \
	"UEFI: Haiku's loader, not another system's BOOTX64.EFI"
check '! grep -q "mount warning" "$work/out"' "mount's warnings stay out of grub.cfg"
run "$work/efi" true
check '[ ! -s "$work/out" ]' "nothing when other systems aren't looked for"
exit $((failures > 0))

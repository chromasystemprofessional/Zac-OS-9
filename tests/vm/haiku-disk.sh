#!/bin/sh
# A virtual hard disk with Haiku on it and the rest free, for trying the
# installer's "next to what's on it" (issue #42) in a VM. Made from Haiku's
# anyboot image, with no installer to click through and no root:
#
#   tests/vm/haiku-disk.sh haiku-r1beta5-x86_64-anyboot.iso haiku-bios.qcow2 bios
#   tests/vm/haiku-disk.sh haiku-r1beta5-x86_64-anyboot.iso haiku-uefi.qcow2 uefi [SIZE_GB]
#   tests/vm/haiku-disk.sh ANYBOOT.iso windows.qcow2 uefi-windows [SIZE_GB]
#
# bios: an MBR disk, Haiku's partition at the very offset it has in the
#       image (its boot block knows where it is) with the image's MBR code,
#       so a BIOS starts Haiku from it as from the image.
# uefi: a GPT disk, an EFI System partition copied from the image (Haiku's
#       loader as EFI/BOOT/BOOTX64.EFI; only 2.8 MB, too small for GRUB)
#       and Haiku's partition after it.
# uefi-windows: the same, then an NTFS partition named Windows filling the
#       rest, holding random test files whose SHA-256 sums go in
#       OUT.sha256 (check them after an install with ntfscat): no free
#       space, so the installer has to make some by shrinking it.
# The disk is SIZE_GB (40) in all; what Haiku doesn't use is free space.
set -eu
[ "$#" -ge 3 ] || { echo "usage: haiku-disk.sh ANYBOOT.iso OUT.qcow2 bios|uefi|uefi-windows [SIZE_GB]" >&2; exit 64; }
image=$1 out=$2 firmware=$3 size_gb=${4:-40}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The image's partitions: start and size in 512-byte sectors, by type.
part() {
	sfdisk -d "$image" | awk -v t="$1" '
		$0 ~ "type=" t "($|,| )" {
			for (i = 1; i <= NF; i++) {
				if ($i == "start=") s = $(i + 1); else if ($i ~ /^start=/) { split($i, a, "="); s = a[2] }
				if ($i == "size=") n = $(i + 1); else if ($i ~ /^size=/) { split($i, a, "="); n = a[2] }
			}
			gsub(/,/, "", s); gsub(/,/, "", n); print s, n; exit
		}'
}
set -- $(part eb)
[ "$#" = 2 ] || { echo "haiku-disk.sh: no Haiku (BFS, type eb) partition in $image" >&2; exit 1; }
bfs_start=$1 bfs_size=$2

raw=$work/disk.img
truncate -s "${size_gb}G" "$raw"
case $firmware in
bios)
	# The image's boot code (the first 440 bytes), then a new table.
	dd if="$image" of="$raw" bs=440 count=1 conv=notrunc status=none
	printf 'label: dos\nstart=%s, size=%s, type=eb, bootable\n' "$bfs_start" "$bfs_size" |
		sfdisk -q --no-reread "$raw"
	dd if="$image" of="$raw" bs=512 skip="$bfs_start" seek="$bfs_start" count="$bfs_size" \
		conv=notrunc status=none
	;;
uefi | uefi-windows)
	set -- $(part ef)
	[ "$#" = 2 ] || { echo "haiku-disk.sh: no EFI System partition (type ef) in $image" >&2; exit 1; }
	esp_start=$1 esp_size=$2
	# 2048 sectors in, as tools make them; Haiku's after, on the next MiB.
	haiku=$(( (2048 + esp_size + 2047) / 2048 * 2048 ))
	printf 'label: gpt\nstart=2048, size=%s, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B\nstart=%s, size=%s, type=42465331-3BA3-10F1-802A-4861696B7521, name="Haiku"\n' \
		"$esp_size" "$haiku" "$bfs_size" | sfdisk -q --no-reread "$raw"
	dd if="$image" of="$raw" bs=512 skip="$esp_start" seek=2048 count="$esp_size" conv=notrunc status=none
	dd if="$image" of="$raw" bs=512 skip="$bfs_start" seek="$haiku" count="$bfs_size" conv=notrunc status=none
	if [ "$firmware" = uefi-windows ]; then
		ntfs=$(( (haiku + bfs_size + 2047) / 2048 * 2048 ))
		total=$(( size_gb * 1024 * 1024 * 2 ))
		ntfs_size=$(( total - 2048 - ntfs )) # GPT keeps the last MiB
		printf 'start=%s, size=%s, type=EBD0A0A2-B9E5-4433-87C0-68B6B72699C7, name="Basic data partition"\n' \
			"$ntfs" "$ntfs_size" | sfdisk -q --no-reread --append "$raw"
		vol=$work/ntfs.img
		truncate -s $((ntfs_size * 512)) "$vol"
		mkntfs -q -F -Q -L Windows -p "$ntfs" "$vol" >/dev/null 2>&1
		# Files to find again: some small, one big, all random.
		: >"$out.sha256"
		for i in 1 2 3 4 5 6 7 8; do
			head -c $((i * 300000)) /dev/urandom >"$work/file$i.bin"
		done
		head -c 300000000 /dev/urandom >"$work/big.bin"
		for f in "$work"/*.bin; do
			ntfscp -q -f "$vol" "$f" "${f##*/}"
			(cd "$work" && sha256sum "${f##*/}") >>"$out.sha256"
		done
		dd if="$vol" of="$raw" bs=1M seek=$((ntfs / 2048)) conv=notrunc,sparse status=none
		cat "$out.sha256"
	fi
	;;
*)
	echo "haiku-disk.sh: bios, uefi or uefi-windows, not $firmware" >&2
	exit 64
	;;
esac
qemu-img convert -O qcow2 "$raw" "$out"
sfdisk -l "$raw" | sed -n '/^Device/,$p'

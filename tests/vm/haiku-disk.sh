#!/bin/sh
# A virtual hard disk with Haiku on it and the rest free, for trying the
# installer's "next to what's on it" (issue #42) in a VM. Made from Haiku's
# anyboot image, with no installer to click through and no root:
#
#   tests/vm/haiku-disk.sh haiku-r1beta5-x86_64-anyboot.iso haiku-bios.qcow2 bios
#   tests/vm/haiku-disk.sh haiku-r1beta5-x86_64-anyboot.iso haiku-uefi.qcow2 uefi [SIZE_GB]
#
# bios: an MBR disk, Haiku's partition at the very offset it has in the
#       image (its boot block knows where it is) with the image's MBR code,
#       so a BIOS starts Haiku from it as from the image.
# uefi: a GPT disk, an EFI System partition copied from the image (Haiku's
#       loader as EFI/BOOT/BOOTX64.EFI) and Haiku's partition after it.
# The disk is SIZE_GB (40) in all; what Haiku doesn't use is free space.
set -eu
[ "$#" -ge 3 ] || { echo "usage: haiku-disk.sh ANYBOOT.iso OUT.qcow2 bios|uefi [SIZE_GB]" >&2; exit 64; }
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
uefi)
	set -- $(part ef)
	[ "$#" = 2 ] || { echo "haiku-disk.sh: no EFI System partition (type ef) in $image" >&2; exit 1; }
	esp_start=$1 esp_size=$2
	# 2048 sectors in, as tools make them; Haiku's after, on the next MiB.
	haiku=$(( (2048 + esp_size + 2047) / 2048 * 2048 ))
	printf 'label: gpt\nstart=2048, size=%s, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B\nstart=%s, size=%s, type=42465331-3BA3-10F1-802A-4861696B7521, name="Haiku"\n' \
		"$esp_size" "$haiku" "$bfs_size" | sfdisk -q --no-reread "$raw"
	dd if="$image" of="$raw" bs=512 skip="$esp_start" seek=2048 count="$esp_size" conv=notrunc status=none
	dd if="$image" of="$raw" bs=512 skip="$bfs_start" seek="$haiku" count="$bfs_size" conv=notrunc status=none
	;;
*)
	echo "haiku-disk.sh: bios or uefi, not $firmware" >&2
	exit 64
	;;
esac
qemu-img convert -O qcow2 "$raw" "$out"
sfdisk -l "$raw" | sed -n '/^Device/,$p'

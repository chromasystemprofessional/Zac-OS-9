#!/bin/sh
# Builds setos.efi beside this script with gnu-efi (apt install gnu-efi).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/setos.efi}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
lib=/usr/lib
[ -f "$lib/crt0-efi-x86_64.o" ] || lib=/usr/lib/x86_64-linux-gnu
[ -f "$lib/crt0-efi-x86_64.o" ] || { echo "build-setos: gnu-efi isn't installed (apt install gnu-efi)" >&2; exit 1; }
gcc -c "$here/setos.c" -o "$work/setos.o" \
	-I/usr/include/efi -I/usr/include/efi/x86_64 \
	-fpic -ffreestanding -fno-stack-protector -fno-stack-check -fshort-wchar -mno-red-zone \
	-maccumulate-outgoing-args -DGNU_EFI_USE_MS_ABI -O2 -Wall
ld -shared -Bsymbolic -nostdlib -znocombreloc -L"$lib" -T "$lib/elf_x86_64_efi.lds" \
	"$lib/crt0-efi-x86_64.o" "$work/setos.o" -o "$work/setos.so" -lefi -lgnuefi
objcopy -j .text -j .sdata -j .data -j .rodata -j .dynamic -j .dynsym -j .rel -j .rela \
	-j '.rel.*' -j '.rela.*' -j .reloc --target efi-app-x86_64 --subsystem=10 "$work/setos.so" "$out"
echo "built $out"

#!/bin/sh
# Package SheepShaver and Basilisk II (emulation/build-emulators.sh) as
# platinum-emulators, installed in /usr/libexec/platinum where
# platinum-classic looks for them. Also writes the emulators' source
# (GPL-2) next to the package, to go wherever the package goes.
#
#   sh packaging/emulators/build-deb.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=$root/build/packages
src=$root/build/emulators/macemu
mkdir -p "$out"
rm -f "$out"/platinum-emulators_*

sh "$root/emulation/build-emulators.sh"

commit=$(git -C "$src" rev-parse HEAD)
date=$(git -C "$src" log -1 --format=%cd --date=format:%Y%m%d)
version=0~git$date.$(echo "$commit" | cut -c1-7)
arch=$(dpkg --print-architecture)
name=platinum-emulators

work=$(mktemp -d /tmp/platinum-emu.XXXXXX)
trap 'rm -rf "$work"' EXIT
pkg=$work/pkg
mkdir -p "$pkg/DEBIAN" "$pkg/usr/libexec/platinum" "$pkg/usr/share/doc/$name"
install -m 755 -s "$root/build/emulators/bin/SheepShaver" "$root/build/emulators/bin/BasiliskII" \
	"$pkg/usr/libexec/platinum/"
sed -e "s/@COMMIT@/$commit/" -e "s/@VERSION@/$version/" "$here/copyright" \
	>"$pkg/usr/share/doc/$name/copyright"
cat <<EOF | gzip -9n >"$pkg/usr/share/doc/$name/changelog.gz"
$name ($version) trixie; urgency=medium

  * SheepShaver and Basilisk II from macemu commit $commit.

 -- adamjlawson-ctrl <adamjlawson@gmail.com>  $(git -C "$src" log -1 --format=%cD)
EOF

# Shared-library dependencies, the way debhelper would find them.
mkdir -p "$work/debian"
printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "$name" "$name" >"$work/debian/control"
deps=$(cd "$work" && dpkg-shlibdeps -O "$pkg"/usr/libexec/platinum/* 2>/dev/null |
	sed -n 's/^shlibs:Depends=//p')

cat >"$pkg/DEBIAN/control" <<EOF
Package: $name
Version: $version
Architecture: $arch
Maintainer: adamjlawson-ctrl <adamjlawson@gmail.com>
Installed-Size: $(du -ks "$pkg/usr" | cut -f1)
Depends: $deps
Enhances: platinum-2026
Section: otherosfs
Priority: optional
Homepage: https://github.com/kanjitalk755/macemu
Description: classic Macintosh emulators for Platinum 2026
 SheepShaver (PowerPC, Mac OS 7.5.2 to 9.0.4) and Basilisk II (68k,
 System 7 to Mac OS 8.1) from the macemu project, built for the
 platinum-classic launcher.
 .
 No Macintosh ROMs or system software are included: you supply your
 own in ~/Classic.
EOF

dpkg-deb --root-owner-group --build "$pkg" "$out/${name}_${version}_$arch.deb"
git -C "$src" archive --prefix="macemu-$commit/" -o "$out/${name}_${version}.source.tar.gz" "$commit"

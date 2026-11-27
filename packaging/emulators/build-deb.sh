#!/bin/sh
# Package SheepShaver and Basilisk II (emulation/build-emulators.sh) as
# zacos9-emulators, installed in /usr/libexec/zacos9 where
# zacos9-classic looks for them. Also writes the emulators' source
# (GPL-2) next to the package, to go wherever the package goes.
#
#   sh packaging/emulators/build-deb.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=$root/build/packages
src=$root/build/emulators/macemu
mkdir -p "$out"
rm -f "$out"/zacos9-emulators_*

sh "$root/emulation/build-emulators.sh"

commit=$(git -C "$src" rev-parse HEAD)
date=$(git -C "$src" log -1 --format=%cd --date=format:%Y%m%d)
version=0~git$date.$(echo "$commit" | cut -c1-7)
arch=$(dpkg --print-architecture)
name=zacos9-emulators

work=$(mktemp -d /tmp/zacos9-emu.XXXXXX)
trap 'rm -rf "$work"' EXIT
pkg=$work/pkg
mkdir -p "$pkg/DEBIAN" "$pkg/usr/libexec/zacos9" "$pkg/usr/share/doc/$name"
install -m 755 -s "$root/build/emulators/bin/SheepShaver" "$root/build/emulators/bin/BasiliskII" \
	"$pkg/usr/libexec/zacos9/"
sed -e "s/@COMMIT@/$commit/" -e "s/@VERSION@/$version/" "$here/copyright" \
	>"$pkg/usr/share/doc/$name/copyright"
cat <<EOF | gzip -9n >"$pkg/usr/share/doc/$name/changelog.gz"
$name ($version) trixie; urgency=medium

  * SheepShaver and Basilisk II from macemu commit $commit.
  * SheepShaver includes the ZacOS 9 Classic bridge patch.

 -- adamjlawson-ctrl <adamjlawson@gmail.com>  $(git -C "$src" log -1 --format=%cD)
EOF

# Shared-library dependencies, the way debhelper would find them.
mkdir -p "$work/debian"
printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "$name" "$name" >"$work/debian/control"
deps=$(cd "$work" && dpkg-shlibdeps -O "$pkg"/usr/libexec/zacos9/* 2>/dev/null |
	sed -n 's/^shlibs:Depends=//p')

cat >"$pkg/DEBIAN/control" <<EOF
Package: $name
Version: $version
Architecture: $arch
Maintainer: adamjlawson-ctrl <adamjlawson@gmail.com>
Installed-Size: $(du -ks "$pkg/usr" | cut -f1)
Depends: $deps, libcap2-bin
Enhances: zacos9-session, zacos9
Section: otherosfs
Priority: optional
Homepage: https://github.com/kanjitalk755/macemu
Description: classic Macintosh emulators for ZacOS 9
 SheepShaver (PowerPC, Mac OS 7.5.2 to 9.0.4) and Basilisk II (68k,
 System 7 to Mac OS 8.1) from the macemu project, built for the
 zacos9-classic launcher.
 .
 No Macintosh ROMs or system software are included: you supply your
 own in ~/Classic.
EOF

# An Old World ROM needs SheepShaver to map low memory (see zacos9's postinst).
cat >"$pkg/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = configure ] &&
		! setcap cap_sys_rawio=ep /usr/libexec/zacos9/SheepShaver 2>/dev/null; then
	echo "zacos9-emulators: could not give SheepShaver the low-memory permission;" \
		"Old Dog may not start with an Old World ROM." >&2
fi
exit 0
EOF
chmod 755 "$pkg/DEBIAN/postinst"

dpkg-deb --root-owner-group --build "$pkg" "$out/${name}_${version}_$arch.deb"

# GPL source: the pinned tree plus the Classic bridge changes that were
# actually built. Include the patch itself, and overlay the two files it
# touches so the archive matches the binaries.
srcstage=$work/source/macemu-$commit
mkdir -p "$work/source"
git -C "$src" archive --format=tar --prefix="macemu-$commit/" \
	-o "$work/source.tar" "$commit"
tar -C "$work/source" -xf "$work/source.tar"
install -D -m 644 "$src/SheepShaver/src/emul_op.cpp" \
	"$srcstage/SheepShaver/src/emul_op.cpp"
install -D -m 644 "$src/SheepShaver/src/include/emul_op.h" \
	"$srcstage/SheepShaver/src/include/emul_op.h"
install -D -m 644 "$src/SheepShaver/src/Unix/video_x.cpp" \
	"$srcstage/SheepShaver/src/Unix/video_x.cpp"
for sdl in video_sdl.cpp video_sdl2.cpp video_sdl3.cpp; do
	install -D -m 644 "$src/BasiliskII/src/SDL/$sdl" \
		"$srcstage/BasiliskII/src/SDL/$sdl"
done
cp "$root/emulation/patches/sheepshaver-classic-bridge.patch" \
	"$srcstage/zacos9-classic-bridge.patch"
tar -C "$work/source" -czf "$out/${name}_${version}.source.tar.gz" \
	"macemu-$commit"

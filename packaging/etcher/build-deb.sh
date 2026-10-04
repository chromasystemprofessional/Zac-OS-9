#!/bin/sh
# Package balenaEtcher (writes disk images to USB sticks and SD cards) for
# ZacOS 9, from balena's own Linux build, so it comes preinstalled: it
# shows in Applications > Utilities.
#
#   sh packaging/etcher/build-deb.sh            # downloads the release
#   ETCHER_ZIP=~/Downloads/balenaEtcher-linux-x64-2.1.7.zip sh packaging/etcher/build-deb.sh
#
# The zip is checked against the SHA-256 GitHub publishes for it. The
# package is named and laid out as balena's own .deb (balena-etcher, in
# /usr/lib/balena-etcher, which Debian's AppArmor profile expects), so
# installing theirs later simply upgrades it. Apache-2.0; the licence
# goes with it.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=$root/build/packages

version=2.1.7
url=https://github.com/balena-io/etcher/releases/download/v$version/balenaEtcher-linux-x64-$version.zip
sha256=6602dc2195f422daf7c5bc3eaf0eaff1def7ccd8aba0cd9b36e8966b4e0e6d16
name=balena-etcher
arch=amd64

mkdir -p "$out" "$root/build/etcher"
rm -f "$out"/${name}_*
zip=${ETCHER_ZIP:-$root/build/etcher/balenaEtcher-linux-x64-$version.zip}
if [ ! -f "$zip" ]; then
	echo "Downloading balenaEtcher $version..."
	wget -q -O "$zip.part" "$url"
	mv "$zip.part" "$zip"
fi
echo "$sha256  $zip" | sha256sum -c --quiet - ||
	{ echo "build-deb.sh: $zip isn't balena's balenaEtcher $version (checksum differs)." >&2; exit 1; }

work=$(mktemp -d /tmp/zacos9-etcher.XXXXXX)
trap 'rm -rf "$work"' EXIT
pkg=$work/pkg
lib=$pkg/usr/lib/$name
mkdir -p "$pkg/DEBIAN" "$pkg/usr/bin" "$pkg/usr/lib" "$pkg/usr/share/applications" \
	"$pkg/usr/share/icons/hicolor/256x256/apps" "$pkg/usr/share/doc/$name"
python3 - "$zip" "$work/unzipped" <<'EOF'
import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    for info in z.infolist():
        target = z.extract(info, sys.argv[2])
        mode = (info.external_attr >> 16) & 0o777
        if mode and not info.is_dir():
            os.chmod(target, mode)
EOF
mv "$work/unzipped/balenaEtcher-linux-x64" "$lib"
# Chromium's sandbox helper must be setuid root, as in balena's own .deb.
chmod 4755 "$lib/chrome-sandbox"
ln -s ../lib/$name/balena-etcher "$pkg/usr/bin/balena-etcher"

# The icon: balena's own (assets/icon.png at the v2.1.7 tag; the Linux
# zip carries none, only their .deb does).
cp "$here/balena-etcher.png" "$pkg/usr/share/icons/hicolor/256x256/apps/$name.png"

cat >"$pkg/usr/share/applications/$name.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=balenaEtcher
Comment=Flash OS images to SD cards and USB drives, safely and easily
Exec=/usr/lib/$name/balena-etcher %U
Icon=$name
Terminal=false
Categories=Utility;X-ZacOS9-Utility;
StartupWMClass=balenaEtcher
EOF

cp "$lib/LICENSE" "$pkg/usr/share/doc/$name/copyright"
find "$pkg/usr/share" "$pkg/usr/bin" -type d -exec chmod 755 {} +
find "$pkg/usr/share" -type f -exec chmod 644 {} +
printf '%s (%s) trixie; urgency=medium\n\n  * balenaEtcher %s, repackaged from balena'"'"'s Linux build for ZacOS 9.\n\n -- adamjlawson-ctrl <adamjlawson@gmail.com>  %s\n' \
	"$name" "$version" "$version" "$(date -R)" | gzip -9n >"$pkg/usr/share/doc/$name/changelog.gz"

# Shared-library dependencies, the way debhelper would find them (Electron
# opens some libraries itself; those are listed by hand, as balena does).
mkdir -p "$work/debian"
printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "$name" "$name" >"$work/debian/control"
deps=$(cd "$work" && dpkg-shlibdeps -O -l"$lib" "$lib/balena-etcher" 2>/dev/null |
	sed -n 's/^shlibs:Depends=//p')
cat >"$pkg/DEBIAN/control" <<EOF
Package: $name
Version: $version
Architecture: $arch
Maintainer: adamjlawson-ctrl <adamjlawson@gmail.com>
Installed-Size: $(du -ks "$pkg/usr" | cut -f1)
Depends: ${deps:+$deps, }libgtk-3-0t64 | libgtk-3-0, libnotify4, libnss3, libxss1, libxtst6, xdg-utils, libatspi2.0-0t64 | libatspi2.0-0, libsecret-1-0
Section: utils
Priority: optional
Homepage: https://etcher.balena.io
Description: Flash OS images to SD cards and USB drives
 balenaEtcher writes disk images (.img, .iso, .zip) to USB sticks and SD
 cards, checking what it wrote, and won't write to the computer's own
 hard disks by accident. balena's Linux build, packaged for ZacOS 9.
EOF

dpkg-deb --root-owner-group --build "$pkg" "$out/${name}_${version}_$arch.deb"
ls -l "$out/${name}_${version}_$arch.deb"

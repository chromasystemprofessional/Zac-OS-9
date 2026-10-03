#!/bin/sh
# Publish the newest ISO in build/iso to SourceForge's file release service,
# with a SHA-256 checksum beside it. Run inside WSL Debian (or any Linux)
# after scripts/build-iso.sh:
#
#   scripts/release-iso.sh SF_USER SF_PROJECT [--dry-run]
#
# SF_USER is your SourceForge username and SF_PROJECT the project's short
# name (the "xyz" in sourceforge.net/projects/xyz). It uploads over ssh
# (rsync), so add your public key at sourceforge.net/auth/ssh first; nothing
# here asks for or stores a password. The files land in
# /home/frs/project/SF_PROJECT/VERSION/, which the project's Files page
# shows. --dry-run only lists what would be sent.
set -eu
self=$(realpath "$0")
cd "$(dirname "$self")/.."

[ $# -ge 2 ] || { sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 64; }
user=$1
project=$2
dry=
[ "${3:-}" = "--dry-run" ] && dry=--dry-run

command -v rsync >/dev/null || { echo "rsync is needed: sudo apt install rsync" >&2; exit 2; }
iso=$(ls -t build/iso/*.iso 2>/dev/null | head -1)
[ -n "$iso" ] || { echo "No ISO in build/iso: run scripts/build-iso.sh first." >&2; exit 2; }
version=$(dpkg-parsechangelog -S Version)
name=zacos9-$version-amd64.iso

out=build/release
mkdir -p "$out"
rm -f "$out"/*.iso "$out"/*.sha256
ln "$iso" "$out/$name" 2>/dev/null || cp "$iso" "$out/$name"
(cd "$out" && sha256sum "$name" > "$name.sha256")
cat "$out/$name.sha256"

# --mkpath makes the VERSION folder (rsync 3.2.3 and later).
rsync -avP $dry --mkpath -e ssh "$out/$name" "$out/$name.sha256" \
	"$user@frs.sourceforge.net:/home/frs/project/$project/$version/"

echo
echo "Done. Download page: https://sourceforge.net/projects/$project/files/$version/"
echo "Mark $name as the default download for all platforms on that Files page"
echo "(the (i) button next to it) so the project's green Download button gets it."

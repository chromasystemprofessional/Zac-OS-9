#!/bin/sh
# Publish a ZacOS 9 release, which is what Software Update (zacos9-update)
# offers every installed system:
#
#   scripts/release.sh 0.2.0 "What's new, one or more sentences."
#
# On a clean main branch, it adds a debian/changelog entry for the version,
# commits and tags it (v0.2.0), builds the package, pushes, and creates the
# GitHub release with zacos9_0.2.0_amd64.deb attached. Needs `gh auth login`.
# See docs/updates.md.
set -eu
self=$(realpath "$0")
cd "$(dirname "$self")/.."

die() {
	echo "release.sh: $1" >&2
	exit 1
}

[ "$#" -ge 1 ] || die "usage: scripts/release.sh VERSION [NOTES]"
version=$1
notes=${2:-"ZacOS 9 $version."}
case "$version" in
	[0-9]*) ;;
	*) die "'$version' must start with a digit, e.g. 0.2.0." ;;
esac
case "$version" in
	*[!A-Za-z0-9.+~]*) die "'$version' isn't a Debian version (letters, digits, . + ~)." ;;
esac

command -v gh >/dev/null || die "gh isn't installed (sudo apt install gh)."
gh auth status >/dev/null 2>&1 || die "not logged in to GitHub: run gh auth login."
[ "$(git rev-parse --abbrev-ref HEAD)" = main ] || die "releases are made from main."
[ -z "$(git status --porcelain)" ] || die "commit or stash your changes first."
git fetch -q origin
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || die "main isn't the same as origin/main; pull or push first."
git rev-parse -q --verify "refs/tags/v$version" >/dev/null && die "v$version already exists."

current=$(dpkg-parsechangelog -S Version)
dpkg --compare-versions "$version" gt "$current" || die "$version isn't newer than $current."

# The changelog entry: debian/changelog's own format, newest first.
name=$(git config user.name)
email=$(git config user.email)
{
	printf 'zacos9 (%s) trixie; urgency=medium\n\n' "$version"
	printf '%s\n' "$notes" | fold -s -w 72 | sed 's/ *$//; s/^/  /; 1s/^  /  * /; 2,$s/^  /    /'
	printf '\n -- %s <%s>  %s\n\n' "$name" "$email" "$(date -R)"
	cat debian/changelog
} >debian/changelog.new
mv debian/changelog.new debian/changelog

git commit -q -m "Release ZacOS 9 $version" \
	-m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>" -- debian/changelog
git tag -a "v$version" -m "ZacOS 9 $version"

scripts/build-debs.sh
deb=build/packages/zacos9_${version}_$(dpkg --print-architecture).deb
[ -f "$deb" ] || die "the build didn't make $deb."

git push -q origin main "v$version"
gh release create "v$version" "$deb" --title "ZacOS 9 $version" --notes "$notes"
echo "Released ZacOS 9 $version: Software Update will offer it."

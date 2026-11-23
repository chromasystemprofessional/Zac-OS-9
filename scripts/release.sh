#!/bin/sh
# Publish a ZacOS 9 release, which is what Software Update (zacos9-update)
# offers every installed system:
#
#   scripts/release.sh [--transition|--split] 0.2.0 "What's new, one or more sentences."
#
# On a clean main branch, it adds a debian/changelog entry for the version,
# moves the split packages' zacos9 Breaks/Replaces to it, commits and tags
# it, builds the package, pushes, and creates the GitHub release. A
# transition release (the default) tags 0.2.0, publishes only the
# zacos9_0.2.0_amd64.deb monolith and SHA256SUMS, and keeps the split packages
# for the signed repository. A split release (--split, once pre-1.18.30
# updaters are no longer supported) tags v0.2.0 and publishes the split
# package set. Needs `gh auth login`. See docs/updates.md.
set -eu
self=$(realpath "$0")
cd "$(dirname "$self")/.."

die() {
	echo "release.sh: $1" >&2
	exit 1
}

transition=true
case "${1:-}" in
--transition) shift ;;
--split) transition=false; shift ;;
esac
[ "$#" -ge 1 ] || die "usage: scripts/release.sh [--transition|--split] VERSION [NOTES]"
version=$1
notes=${2:-"ZacOS 9 $version."}
case "$version" in
	[0-9]*) ;;
	*) die "'$version' must start with a digit, e.g. 0.2.0." ;;
esac
case "$version" in
	*[!A-Za-z0-9.+~]*) die "'$version' isn't a Debian version (letters, digits, . + ~)." ;;
esac
if $transition; then
	tag=$version
else
	tag=v$version
fi

command -v gh >/dev/null || die "gh isn't installed (sudo apt install gh)."
gh auth status >/dev/null 2>&1 || die "not logged in to GitHub: run gh auth login."
[ "$(git rev-parse --abbrev-ref HEAD)" = main ] || die "releases are made from main."
[ -z "$(git status --porcelain)" ] || die "commit or stash your changes first."
git fetch -q origin
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || die "main isn't the same as origin/main; pull or push first."
git rev-parse -q --verify "refs/tags/$tag" >/dev/null && die "$tag already exists."

current=$(dpkg-parsechangelog -S Version)
dpkg --compare-versions "$version" gt "$current" || die "$version isn't newer than $current."

if $transition; then
	notes="$notes The GitHub release is a transition monolith built from this
release's split packages for computers whose updater is older than 1.18.30."
fi

# The split packages must take back the files from the last monolith, so a
# repository install can replace it with this version's session and greeter.
if ! grep -q '^Breaks: zacos9 (<< ' debian/control ||
		! grep -q '^Replaces: zacos9 (<< ' debian/control; then
	die "debian/control has no zacos9 Breaks/Replaces lines to update."
fi
sed -i "s/^Breaks: zacos9 (<< .*)$/Breaks: zacos9 (<< $version)/" debian/control
sed -i "s/^Replaces: zacos9 (<< .*)$/Replaces: zacos9 (<< $version)/" debian/control

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

git commit -q -m "Release ZacOS 9 $version" -- debian/changelog debian/control
git tag -a "$tag" -m "ZacOS 9 $version"

scripts/build-debs.sh
arch=$(dpkg --print-architecture)
[ "$arch" = amd64 ] || die "transition releases are built for amd64, not $arch."
if $transition; then
	out=build/transition
	rm -rf "$out"
	mkdir -p "$out"
	scripts/build-transition-package.py --version "$version" --base-version "$version" \
		--debs build/packages --output "$out"
	deb=$out/zacos9_${version}_${arch}.deb
	[ -f "$deb" ] || die "the build didn't make $deb."
	(cd "$out" && sha256sum "zacos9_${version}_${arch}.deb" >SHA256SUMS)
	assets="$deb $out/SHA256SUMS"
else
	debs=$(find build/packages -maxdepth 1 \
		-name "zacos9*_${version}_${arch}.deb" ! -name "*-dbgsym_*.deb" -print)
	[ -n "$debs" ] || die "the build didn't make the $version packages."
	assets=$debs
fi

git push -q origin main "$tag"
# shellcheck disable=SC2086 # one path per package, none contains a space
gh release create "$tag" $assets --title "ZacOS 9 $version" --notes "$notes"
if $transition; then
	echo "Released ZacOS 9 $version as a transition monolith."
	echo "Publish the split packages in build/packages to the signed repository."
else
	echo "Released ZacOS 9 $version: Software Update will offer it."
fi

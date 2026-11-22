#!/bin/sh
# Build a signed flat Debian repository from the built packages.
#
#   scripts/build-repo.sh --key FINGERPRINT [options]
#
# The output holds the .deb files, Packages, Packages.gz, Release, InRelease,
# Release.gpg and the public key. A .deb already in the output is kept, so a
# new release adds to the repository instead of replacing it. The output must
# be a clean directory, not a git checkout: apt-ftparchive would otherwise
# checksum .git.
#
# The signing key must be in GNUPGHOME. Leave ZACOS9_REPO_GPG_PASSPHRASE
# unset to let gpg prompt (a local run); set it, even empty, for a
# non-interactive run such as CI. Debug-symbol packages are left out unless
# ZACOS9_REPO_DEBUG=1.
#
#   --packages DIR   where the .deb files are (default: build/packages)
#   --output DIR     where the repository is written (default: build/repo)
#   --key KEY        signing key fingerprint or id (or ZACOS9_REPO_KEY)
#   --suite NAME     Release Suite field (default: stable)
#   --codename NAME  Release Codename field (default: trixie)
#   --origin NAME    Release Origin field (default: ZacOS)
#   --label NAME     Release Label field (default: ZacOS 9)
set -eu
self=$(realpath "$0")
cd "$(dirname "$self")/.."

die() {
	echo "build-repo.sh: $1" >&2
	exit 1
}

usage() {
	awk 'NR == 1 { next }
	     /^set -eu$/ { exit }
	     { sub(/^# ?/, ""); print }' "$self"
}

packages=build/packages
output=build/repo
key=${ZACOS9_REPO_KEY:-}
suite=${ZACOS9_REPO_SUITE:-stable}
codename=${ZACOS9_REPO_CODENAME:-trixie}
origin=${ZACOS9_REPO_ORIGIN:-ZacOS}
label=${ZACOS9_REPO_LABEL:-ZacOS 9}

while [ "$#" -gt 0 ]; do
	case $1 in
	--packages | --output | --key | --suite | --codename | --origin | --label)
		[ "$#" -ge 2 ] || die "$1 needs a value"
		;;
	esac
	case $1 in
	--packages) packages=$2; shift 2 ;;
	--output) output=$2; shift 2 ;;
	--key) key=$2; shift 2 ;;
	--suite) suite=$2; shift 2 ;;
	--codename) codename=$2; shift 2 ;;
	--origin) origin=$2; shift 2 ;;
	--label) label=$2; shift 2 ;;
	-h | --help) usage; exit 0 ;;
	*) die "unknown option: $1" ;;
	esac
done

[ -n "$key" ] || die "no signing key: pass --key or set ZACOS9_REPO_KEY"
for tool in apt-ftparchive gpg gzip awk; do
	command -v "$tool" >/dev/null || die "$tool isn't installed."
done
gpg --batch --list-secret-keys "$key" >/dev/null 2>&1 ||
	die "the signing key $key isn't in GNUPGHOME."
[ -d "$packages" ] || die "no package directory $packages."
[ ! -e "$output/.git" ] ||
	die "the output is a git checkout; use a clean directory and copy the result in."

mkdir -p "$output"
if [ "${ZACOS9_REPO_DEBUG:-}" != 1 ]; then
	find "$output" -maxdepth 1 -type f -name '*-dbgsym_*.deb' -delete
fi
found=false
for deb in "$packages"/*.deb; do
	[ -e "$deb" ] || continue
	case ${deb##*/} in
	*-dbgsym_*.deb)
		[ "${ZACOS9_REPO_DEBUG:-}" = 1 ] || continue
		;;
	esac
	cp -f "$deb" "$output/"
	found=true
done
$found || die "no .deb files in $packages."

# A GPL source tarball belongs beside the binary that needs it.
for source in "$packages"/*.source.tar.gz; do
	[ -e "$source" ] || continue
	cp -f "$source" "$output/"
done

rm -f "$output/Packages" "$output/Packages.gz" "$output/Release" \
	"$output/InRelease" "$output/Release.gpg"
( cd "$output" && apt-ftparchive packages . >Packages )
gzip -9 -n -c "$output/Packages" >"$output/Packages.gz"

architectures=$(sed -n 's/^Architecture: //p' "$output/Packages" | sort -u | paste -sd, -)
[ -n "$architectures" ] || architectures=amd64
# Write Release outside the directory: apt-ftparchive would otherwise
# checksum the empty file the shell just created.
release_tmp=$(mktemp)
( cd "$output" && apt-ftparchive \
	-o "APT::FTPArchive::Release::Origin=$origin" \
	-o "APT::FTPArchive::Release::Label=$label" \
	-o "APT::FTPArchive::Release::Suite=$suite" \
	-o "APT::FTPArchive::Release::Codename=$codename" \
	-o "APT::FTPArchive::Release::Architectures=$architectures" \
	-o "APT::FTPArchive::Release::Components=main" \
	-o "APT::FTPArchive::Release::Description=$label package repository" \
	release . >"$release_tmp" )
mv "$release_tmp" "$output/Release"

gpg_sign() {
	if [ "${ZACOS9_REPO_GPG_PASSPHRASE+set}" != set ]; then
		gpg --yes "$@"
	elif [ -n "$ZACOS9_REPO_GPG_PASSPHRASE" ]; then
		printf '%s' "$ZACOS9_REPO_GPG_PASSPHRASE" |
			gpg --batch --yes --pinentry-mode loopback --passphrase-fd 0 "$@"
	else
		gpg --batch --yes --pinentry-mode loopback --passphrase '' "$@"
	fi
}
gpg_sign --local-user "$key" --clearsign --output "$output/InRelease" \
	"$output/Release"
gpg_sign --local-user "$key" --armor --detach-sign --output "$output/Release.gpg" \
	"$output/Release"
gpg --batch --yes --export "$key" >"$output/zacos9-archive-keyring.gpg"
gpg --batch --yes --armor --export "$key" >"$output/zacos9-archive-keyring.asc"
: >"$output/.nojekyll"

fingerprint=$(gpg --batch --with-colons --list-keys "$key" |
	awk -F: '/^fpr:/ { print $10; exit }')
count=$(find "$output" -maxdepth 1 -name '*.deb' | wc -l)
printf 'Repository: %s\n' "$output"
printf 'Packages:   %s\n' "$count"
printf 'Suite:      %s (%s), %s\n' "$suite" "$codename" "$architectures"
printf 'Key:        %s\n' "${fingerprint:-$key}"

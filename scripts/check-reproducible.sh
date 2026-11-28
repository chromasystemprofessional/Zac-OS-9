#!/bin/sh
# Build the desktop package twice from the same tree, in two directories, and
# compare the .deb files. The changelog's timestamp is SOURCE_DATE_EPOCH and
# debian/rules compiles a fixed source path, so the checkout directory must
# not change the result.
#
#   scripts/check-reproducible.sh [--keep]
#
# Needs the build dependencies (sudo apt build-dep .). Takes two full builds;
# CI runs it weekly and on v* tags.
set -eu
self=$(realpath "$0")
cd "$(dirname "$self")/.."

keep=false
case "${1:-}" in
--keep) keep=true ;;
"") ;;
*)
	echo "usage: scripts/check-reproducible.sh [--keep]" >&2
	exit 2
	;;
esac

for tool in dpkg-buildpackage dpkg-parsechangelog git sha256sum tar; do
	command -v "$tool" >/dev/null ||
		{ echo "check-reproducible.sh: $tool is missing" >&2; exit 1; }
done

# A disk-backed temporary directory without spaces: the build and its tests
# need several gigabytes, and a few helpers split paths on whitespace.
base=${ZACOS9_BUILD_TMPDIR:-${XDG_CACHE_HOME:-$HOME/.cache}/zacos9}
case $base in
*" "*) echo "check-reproducible.sh: $base contains a space; set ZACOS9_BUILD_TMPDIR to a path without one." >&2; exit 1 ;;
esac
mkdir -p "$base"
work=$(mktemp -d "$base/zacos9-repro.XXXXXX")
finish() {
	status=$?
	if [ "$status" -eq 0 ] && ! $keep; then
		rm -rf "$work"
	else
		printf 'check-reproducible.sh: the builds are in %s\n' "$work" >&2
	fi
	exit "$status"
}
trap finish EXIT

# Build from what `git add -A` would commit: tracked and new files, without
# anything .gitignore excludes.
index=$work/index
GIT_INDEX_FILE=$index git read-tree HEAD
GIT_INDEX_FILE=$index git add -A
tree=$(GIT_INDEX_FILE=$index git write-tree)

build() {
	dir=$1
	log=$2
	mkdir -p "$dir"
	git archive "$tree" | tar -x -C "$dir"
	epoch=$(cd "$dir" && dpkg-parsechangelog -S Timestamp)
	status=0
	(
		cd "$dir"
		if command -v xvfb-run >/dev/null 2>&1; then
			SOURCE_DATE_EPOCH=$epoch xvfb-run -a \
				dpkg-buildpackage -b -us -uc -j"$(nproc)" >"$log" 2>&1
		else
			SOURCE_DATE_EPOCH=$epoch \
				dpkg-buildpackage -b -us -uc -j"$(nproc)" >"$log" 2>&1
		fi
	) || status=$?
	if [ "$status" -ne 0 ]; then
		tail -40 "$log" >&2 || true
	fi
	return "$status"
}

collect() {
	mkdir -p "$work/debs-$1"
	find "$work" -maxdepth 1 -name '*.deb' -exec mv {} "$work/debs-$1/" \;
}

echo "Building twice; this takes two full package builds." >&2
build "$work/one" "$work/one.log"
collect one
build "$work/two" "$work/two.log"
collect two

(cd "$work/debs-one" && sha256sum -- *.deb | sort) >"$work/hashes-one"
(cd "$work/debs-two" && sha256sum -- *.deb | sort) >"$work/hashes-two"
if diff -u "$work/hashes-one" "$work/hashes-two" >"$work/hashes.diff"; then
	count=$(wc -l <"$work/hashes-one" | tr -d ' ')
	echo "check-reproducible.sh: $count packages are identical."
	exit 0
fi

cat "$work/hashes.diff" >&2
differing=$(awk '/^-/ && /\.deb/ { print $2; exit }' "$work/hashes.diff")
if [ -n "$differing" ] && command -v diffoscope >/dev/null 2>&1; then
	echo "--- diffoscope $differing ---" >&2
	diffoscope "$work/debs-one/$differing" "$work/debs-two/$differing" 2>&1 |
		head -80 >&2 || true
else
	echo "Install diffoscope to see what differs." >&2
fi
echo "check-reproducible.sh: the two builds differ." >&2
exit 1

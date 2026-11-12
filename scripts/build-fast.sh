#!/bin/sh
# A fast local build for the compile/test loop, kept apart from the debugging
# build in build/.
#
#   scripts/build-fast.sh                 # applications, no test binaries
#   scripts/build-fast.sh zacos9-finder   # one target (Ninja names)
#
# It configures build-fast (or $ZACOS9_FAST_BUILD_DIR) as debugoptimized
# without debug info, with -pipe, and uses ccache and mold/lld when they are
# installed. Tests are build_by_default: false, so a plain run does not build
# them; `meson test -C build-fast` builds the tests it runs.
set -eu
self=$(realpath "$0")
cd "$(dirname "$self")/.."

dir=${ZACOS9_FAST_BUILD_DIR:-build-fast}
if [ ! -f "$dir/build.ninja" ]; then
	link_args=
	if command -v mold >/dev/null 2>&1; then
		link_args="-Dc_link_args=-fuse-ld=mold -Dcpp_link_args=-fuse-ld=mold"
	elif command -v ld.lld >/dev/null 2>&1; then
		link_args="-Dc_link_args=-fuse-ld=lld -Dcpp_link_args=-fuse-ld=lld"
	fi
	setup_args="--buildtype=custom -Doptimization=2 -Ddebug=false -Dc_args=-pipe -Dcpp_args=-pipe"
	# The options string is deliberately word-split by the shell.
	# shellcheck disable=SC2086
	if command -v ccache >/dev/null 2>&1; then
		CC="ccache ${CC:-cc}" CXX="ccache ${CXX:-c++}" \
			meson setup "$dir" $setup_args $link_args
	else
		meson setup "$dir" $setup_args $link_args
	fi
	echo "configured $dir (-O2, no debug info; $(command -v ccache >/dev/null 2>&1 && echo ccache || echo no ccache); $(command -v mold >/dev/null 2>&1 && echo mold || command -v ld.lld >/dev/null 2>&1 && echo lld || echo default linker))"
elif command -v ccache >/dev/null 2>&1 && ! grep -q ccache "$dir/build.ninja"; then
	echo "note: $dir was configured without ccache; remove it to enable it" >&2
fi

ninja -C "$dir" "$@"

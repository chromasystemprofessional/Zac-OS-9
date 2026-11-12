#!/bin/sh
# Build the headless After Dark helper for the ZacOS screen saver.
#
#   build.sh OUTPUT TARGET_DIR [CARGO]
#
# The vendored engine has no crates.io dependencies, so --offline and --locked
# are real guarantees: the build never fetches anything. CARGO is passed by
# Meson so the configure-time program is the one used even when it is outside
# PATH.
set -eu

src=$(cd "$(dirname "$0")" && pwd)
out=${1:?usage: build.sh OUTPUT TARGET_DIR [CARGO]}
target=${2:?usage: build.sh OUTPUT TARGET_DIR [CARGO]}
cargo=${3:-${CARGO:-cargo}}

# rustup keeps its settings and toolchains in RUSTUP_HOME (normally ~/.rustup)
# and uses HOME to find it. A package build can change HOME, so when CARGO is
# rustup's shim under ~/.cargo/bin, point rustup at the .rustup beside it.
case "$cargo" in
*/.cargo/bin/cargo)
	RUSTUP_HOME=${RUSTUP_HOME:-${cargo%/.cargo/bin/cargo}/.rustup}
	export RUSTUP_HOME
	;;
esac

"$cargo" build --release --locked --offline \
	--manifest-path "$src/Cargo.toml" \
	--target-dir "$target" \
	-p ad-saver
cp "$target/release/zacos9-afterdark" "$out"

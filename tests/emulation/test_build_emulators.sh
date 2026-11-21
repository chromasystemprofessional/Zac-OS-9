#!/bin/sh
# build-emulators.sh: a checkout builds into build/emulators/bin; a copy
# downloaded on its own fetches the patch and builds into the user's
# libexec, where zacos9-classic looks. Stand-in apt-get, git, make and curl:
# nothing is downloaded or compiled.
#
#   test_build_emulators.sh BUILD-EMULATORS
set -u
script=$(realpath "$1")

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
bin=$work/bin
home=$work/home
mkdir -p "$bin" "$home"
failures=0

check() {
	if eval "$1"; then
		echo "ok    $2"
	else
		echo "FAIL  $2"
		failures=$((failures + 1))
	fi
}

cat >"$bin/sudo" <<'EOF'
#!/bin/sh
exec "$@"
EOF
cat >"$bin/apt-get" <<'EOF'
#!/bin/sh
exit 0
EOF
cat >"$bin/curl" <<'EOF'
#!/bin/sh
while [ $# -gt 0 ]; do
	case $1 in
	-o) shift; out=$1 ;;
	esac
	shift
done
printf 'stand-in patch\n' >"$out"
EOF
cat >"$bin/git" <<'EOF'
#!/bin/sh
repo=
mode=
while [ $# -gt 0 ]; do
	case $1 in
	-C) repo=$2; shift 2; continue ;;
	-q|--quiet) shift; continue ;;
	--depth) shift 2; continue ;;
	init|remote|rev-parse|fetch|checkout|apply) mode=$1; shift; break ;;
	esac
	shift
done
case $mode in
	init) eval "repo=\${$#}"; mkdir -p "$repo/.git" ;;
	rev-parse) exit 1 ;;
	checkout)
		# Only the checkout from FETCH_HEAD makes the source tree the
		# build needs; the reset before applying the patch changes nothing.
		case " $* " in
		*" --force "*)
			mkdir -p "$repo/SheepShaver/src/Unix" "$repo/BasiliskII/src/Unix"
			for f in "$repo/SheepShaver/src/Unix/autogen.sh" \
					"$repo/SheepShaver/src/Unix/configure" \
					"$repo/BasiliskII/src/Unix/autogen.sh" \
					"$repo/BasiliskII/src/Unix/configure"; do
				printf '#!/bin/sh\nexit 0\n' >"$f"
				chmod +x "$f"
			done
			;;
		esac
		;;
	apply) [ -f "$1" ] || { echo "missing patch: $1" >&2; exit 1; } ;;
esac
exit 0
EOF
cat >"$bin/setcap" <<'EOF'
#!/bin/sh
[ -x "$2" ] || exit 1
exit 0
EOF
cat >"$bin/make" <<'EOF'
#!/bin/sh
case $PWD in
*/SheepShaver/src/Unix) printf '#!/bin/sh\nexit 0\n' >SheepShaver ;;
*/BasiliskII/src/Unix) printf '#!/bin/sh\nexit 0\n' >BasiliskII ;;
esac
chmod +x SheepShaver BasiliskII 2>/dev/null || true
exit 0
EOF
chmod +x "$bin"/*

run() {
	HOME=$1 PATH="$bin:/usr/bin:/bin" sh "$2"
}

# A downloaded copy: no tree around it, so it fetches the patch and writes
# where the desktop's launcher searches.
mkdir -p "$work/standalone"
cp "$script" "$work/standalone/build-emulators.sh"
run "$home" "$work/standalone/build-emulators.sh" >"$work/standalone.log" 2>&1
status=$?
built=$home/.local/libexec/zacos9
check '[ "$status" = 0 ] && [ -x "$built/SheepShaver" ] && [ -x "$built/BasiliskII" ]' \
	"a standalone copy builds into the user's libexec"
check '[ -f "$home/.cache/zacos9/emulators/sheepshaver-classic-bridge.patch" ]' \
	"and fetches the Classic bridge patch"

# A source checkout: builds beside the tree and uses the patch in it.
mkdir -p "$work/tree/emulation/patches"
cp "$script" "$work/tree/emulation/build-emulators.sh"
printf 'stand-in patch\n' >"$work/tree/emulation/patches/sheepshaver-classic-bridge.patch"
: >"$work/tree/meson.build"
mkdir -p "$work/checkout-home"
run "$work/checkout-home" "$work/tree/emulation/build-emulators.sh" >"$work/tree.log" 2>&1
status=$?
check '[ "$status" = 0 ] && [ -x "$work/tree/build/emulators/bin/SheepShaver" ] &&
	[ -x "$work/tree/build/emulators/bin/BasiliskII" ]' \
	"a checkout builds into build/emulators/bin"
check '[ ! -e "$work/checkout-home/.local/libexec/zacos9" ]' \
	"and does not touch the user's libexec"

# sudo sh ./build-emulators.sh, the report's command: refuse it before
# leaving root-owned files behind.
cat >"$bin/id" <<'EOF'
#!/bin/sh
[ "$1" = -u ] && { echo 0; exit 0; }
exec /usr/bin/id "$@"
EOF
chmod +x "$bin/id"
HOME=$home PATH="$bin:/usr/bin:/bin" sh "$work/standalone/build-emulators.sh" \
	>"$work/root.log" 2>&1
status=$?
check '[ "$status" = 1 ] && grep -q "without sudo" "$work/root.log"' \
	"running it with sudo is refused with a plain reason"

exit $((failures != 0))

#!/bin/sh
# zacos9-release-disk (issue #31) against stand-in lsblk, findmnt, swapon,
# swapoff, umount, wipefs, udevadm and dmsetup: nothing real is touched.
#
#   test_release_disk.sh ZACOS9-RELEASE-DISK
set -u
script=$(realpath "$1")
# A made-up NVMe disk: only the stand-ins ever see its name.
device=/dev/nvme9n1

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fake=$work/fake
bin=$work/bin
mkdir -p "$fake" "$bin" "$work/proc"
failures=0

# The stand-ins: what they report comes from $fake, what they did goes to $fake/log.
cat >"$bin/lsblk" <<'EOF'
#!/bin/sh
case "$*" in
*"-o LABEL"*) eval "dev=\${$#}"; awk -v d="$dev" '$1 == d { $1 = ""; sub(/^ /, ""); print }' "$FAKE/labels" ;;
*"-o PATH,TYPE"*) cat "$FAKE/members" ;;
*"-o TYPE"*) cat "$FAKE/type" ;;
esac
EOF
cat >"$bin/findmnt" <<'EOF'
#!/bin/sh
eval "dev=\${$#}"
awk -F '\t' -v d="$dev" '$1 == d { print $2 }' "$FAKE/mounts"
EOF
cat >"$bin/swapon" <<'EOF'
#!/bin/sh
cat "$FAKE/swaps"
EOF
cat >"$bin/umount" <<'EOF'
#!/bin/sh
echo "umount $*" >>"$FAKE/log"
grep -qxF "$1" "$FAKE/busy" && exit 32
exit 0
EOF
for tool in swapoff wipefs udevadm dmsetup; do
	printf '#!/bin/sh\necho "%s $*" >>"$FAKE/log"\n' "$tool" >"$bin/$tool"
done
chmod +x "$bin"/*

d=$device
p=${d}p
reset() {
	rm -rf "$work/rules" "$work/proc"/*
	: >"$fake/log"
	: >"$fake/busy"
	: >"$fake/swaps"
	: >"$fake/labels"
	: >"$fake/mounts"
}
run() {
	FAKE=$fake PATH="$bin:/usr/bin:/bin" ZACOS9_UDEV_RULES_DIR=$work/rules \
		ZACOS9_PROC=$work/proc sh "$script" "$@" >"$work/out" 2>&1
}
check() {
	if eval "$1"; then
		echo "ok: $2"
	else
		echo "FAIL: $2"
		failures=$((failures + 1))
		sed 's/^/    /' "$work/out" "$fake/log"
	fi
}
logged() { grep -qxF "$1" "$fake/log"; }

# A whole disk with ChromeOS Flex's partitions, the live desktop's mounts on them.
reset
echo disk >"$fake/type"
printf '%s disk\n%s part\n%s part\n%s part\n%s part\n' "$d" "${p}1" "${p}3" "${p}5" "${p}8" >"$fake/members"
printf '%s H-STATE\n%s ROOT-A\n' "${p}1" "${p}3" >"$fake/labels"
printf '%s\t/media/live/H-STATE\n%s\t/media/live/ROOT A\n' "${p}1" "${p}3" >"$fake/mounts"
echo "${p}8" >"$fake/swaps"
run "$d"
status=$?
check '[ "$status" = 0 ] && ! grep -q "^error" "$work/out"' "a whole disk in use by the desktop is released"
check 'logged "umount /media/live/H-STATE" && logged "umount /media/live/ROOT A"' \
	"its partitions are unmounted, a name with a space too"
check 'logged "swapoff ${p}8"' "swap on it is turned off"
check 'logged "wipefs -a -q ${p}1" && logged "wipefs -a -q ${p}5" && logged "wipefs -a -q $d"' \
	"the old partitions' and table's signatures are erased"
check 'grep -qF "ENV{DEVNAME}==\"$d\", ENV{UDISKS_IGNORE}=\"1\"" "$work/rules/90-zacos9-install.rules" &&
	grep -qF "ENV{DEVNAME}==\"${p}[0-9]*\", ENV{UDISKS_IGNORE}=\"1\"" "$work/rules/90-zacos9-install.rules"' \
	"the disk and its partitions, old and new, are hidden from UDisks"
check 'logged "udevadm control --reload"' "udev rereads its rules"
run --restore
check '[ ! -e "$work/rules/90-zacos9-install.rules" ] && [ "$(grep -c "udevadm control --reload" "$fake/log")" = 2 ]' \
	"--restore shows the disk to UDisks again"

# --keep (installing beside what's on the disk): released, nothing erased.
reset
echo disk >"$fake/type"
printf '%s disk\n%s part\n%s part\n' "$d" "${p}1" "${p}2" >"$fake/members"
printf '%s Haiku\n' "${p}1" >"$fake/labels"
printf '%s\t/media/live/Haiku\n' "${p}1" >"$fake/mounts"
run --keep "$d"
status=$?
check '[ "$status" = 0 ] && logged "umount /media/live/Haiku"' "--keep unmounts the disk's partitions"
check '! grep -q "^wipefs" "$fake/log"' "--keep erases nothing on the disk"
check 'grep -qF "ENV{DEVNAME}==\"${p}[0-9]*\", ENV{UDISKS_IGNORE}=\"1\"" "$work/rules/90-zacos9-install.rules"' \
	"--keep hides the disk from UDisks too"
echo /media/live/Haiku >"$fake/busy"
run --keep "$d"
check 'grep -q "^error “Haiku” is in use, so it can.t be changed" "$work/out"' "--keep says changed, not erased"

# The running system: refused before anything is changed.
reset
echo disk >"$fake/type"
printf '%s disk\n%s part\n' "$d" "${p}1" >"$fake/members"
printf '%s\t/\n' "${p}1" >"$fake/mounts"
run "$d"
status=$?
check '[ "$status" = 1 ] && grep -q "^error ZacOS is running from" "$work/out"' "the running system's disk is refused"
check '! grep -qE "^(umount|wipefs)" "$fake/log" && [ ! -e "$work/rules/90-zacos9-install.rules" ]' \
	"and nothing on it is touched"

# A volume a program keeps busy: named in the error.
reset
echo disk >"$fake/type"
printf '%s disk\n%s part\n' "$d" "${p}1" >"$fake/members"
printf '%s Archive\n' "${p}1" >"$fake/labels"
printf '%s\t/media/live/Archive\n' "${p}1" >"$fake/mounts"
echo /media/live/Archive >"$fake/busy"
mkdir -p "$work/proc/4242/fd"
echo php >"$work/proc/4242/comm"
ln -s /media/live/Archive/www "$work/proc/4242/cwd"
run "$d"
status=$?
check '[ "$status" = 1 ] && grep -q "^error “Archive” is in use by php" "$work/out"' \
	"a busy volume stops it, naming the program"
check '! grep -q "^wipefs" "$fake/log"' "and the disk isn't erased"

# One partition: only it is released, and it isn't wiped (it's formatted next).
reset
echo part >"$fake/type"
printf '%s part\n' "$d" >"$fake/members"
printf '%s\t/media/live/Old\n' "$d" >"$fake/mounts"
run "$d"
status=$?
check '[ "$status" = 0 ] && logged "umount /media/live/Old" && ! grep -q "^wipefs" "$fake/log"' \
	"a partition is unmounted, not wiped"
check '[ "$(grep -c UDISKS_IGNORE "$work/rules/90-zacos9-install.rules")" = 1 ]' "only it is hidden"

exit $((failures > 0))

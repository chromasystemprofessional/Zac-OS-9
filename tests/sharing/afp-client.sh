#!/bin/sh
# Test platinum-afp against a real server: mount a volume and use it.
#
#   AFP_PASSWORD=... tests/sharing/afp-client.sh SERVER VOLUME [USER]
#
# Tried against Mac OS 9's File Sharing in Classic (127.0.0.1:5548, as its
# owner) and Netatalk (127.0.0.1, as a user and as a guest). Works in a
# new folder on the volume and removes it afterwards; prints PASS or the
# first FAIL.
set -u
server=$1 volume=$2 user=${3:-}
cd "$(dirname "$0")/../.."
afp=$PWD/build/network/platinum-afp
m=$(mktemp -d)
t="$m/afp-test-$$"
fail() {
	echo "FAIL: $1"
	rm -rf "$t" 2>/dev/null
	fusermount3 -u "$m"
	exit 1
}

if [ -n "$user" ]; then
	printf '%s\n' "${AFP_PASSWORD:-}" | "$afp" mount --user "$user" "$server" "$volume" "$m" || exit 1
else
	"$afp" mount "$server" "$volume" "$m" </dev/null || exit 1
fi
df "$m" >/dev/null || fail "free space"
ls "$m" >/dev/null || fail "list the volume"
mkdir "$t" || fail "make a folder"
printf 'Hello from Linux\n' >"$t/note.txt" || fail "write"
[ "$(cat "$t/note.txt")" = "Hello from Linux" ] || fail "read back"
mv "$t/note.txt" "$t/Résumé.txt" || fail "rename to a Mac Roman name"
! mv "$t/Résumé.txt" "$t/Résumé ✓.txt" 2>/dev/null || fail "a name Macs can't have was taken"
dd if=/dev/urandom of="$m.big" bs=1k count=700 2>/dev/null
cp "$m.big" "$t/big.bin" && cmp -s "$m.big" "$t/big.bin" || fail "700K copy"
rm -f "$m.big"
printf 'over\n' >"$t/other.txt" && mv "$t/other.txt" "$t/big.bin" || fail "rename over a file"
[ "$(cat "$t/big.bin")" = "over" ] || fail "renamed-over contents"
truncate -s 2 "$t/big.bin" && [ "$(cat "$t/big.bin")" = "ov" ] || fail "truncate"
touch -d '2001-02-03 04:05' "$t/Résumé.txt" || fail "set the date"
[ "$(date -r "$t/Résumé.txt" +%F)" = 2001-02-03 ] || fail "date kept"
! touch "$t/a name longer than thirty-one characters" 2>/dev/null || fail "a 32+ character name was taken"
rm -r "$t" || fail "remove the folder"
fusermount3 -u "$m" && rmdir "$m"
echo PASS

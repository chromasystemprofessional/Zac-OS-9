#!/bin/sh
# zacos9-modules-helper's extension switch, in scratch files (no root
# needed: ZACOS9_MODULES_LOAD_CONF and ZACOS9_MODPROBE_CONF stand in for
# /etc/modules-load.d and /etc/modprobe.d).
set -eu
helper=$1
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
export ZACOS9_MODULES_LOAD_CONF=$T/modules-load.d/zacos9-extensions.conf
export ZACOS9_MODPROBE_CONF=$T/modprobe.d/zacos9-extensions.conf
fail() { echo "FAIL: $*" >&2; exit 1; }

# Off: blacklisted, and out of modules-load.d.
"$helper" extension bluetooth off btusb bluetooth
[ "$(cat "$ZACOS9_MODPROBE_CONF")" = "$(printf 'blacklist btusb\nblacklist bluetooth')" ] ||
	fail "the blacklist: $(cat "$ZACOS9_MODPROBE_CONF")"
[ ! -e "$ZACOS9_MODULES_LOAD_CONF" ] || [ ! -s "$ZACOS9_MODULES_LOAD_CONF" ] ||
	fail "modules-load.d still names them: $(cat "$ZACOS9_MODULES_LOAD_CONF")"

# On: listed to start, and the blacklist gone.
"$helper" extension bluetooth on btusb bluetooth
[ "$(cat "$ZACOS9_MODULES_LOAD_CONF")" = "$(printf 'btusb\nbluetooth')" ] ||
	fail "modules-load.d: $(cat "$ZACOS9_MODULES_LOAD_CONF")"
if grep -q blacklist "$ZACOS9_MODPROBE_CONF"; then fail "the blacklist is still there"; fi

# Off then on then off again leaves one line per module, never two.
"$helper" extension bluetooth off btusb bluetooth
"$helper" extension bluetooth off btusb bluetooth
[ "$(cat "$ZACOS9_MODPROBE_CONF")" = "$(printf 'blacklist btusb\nblacklist bluetooth')" ] ||
	fail "the blacklist twice: $(cat "$ZACOS9_MODPROBE_CONF")"
"$helper" extension bluetooth on btusb bluetooth
"$helper" extension bluetooth on btusb bluetooth
[ "$(cat "$ZACOS9_MODULES_LOAD_CONF")" = "$(printf 'btusb\nbluetooth')" ] ||
	fail "modules-load.d twice: $(cat "$ZACOS9_MODULES_LOAD_CONF")"

# One extension does not disturb another's lines.
printf 'blacklist iwlwifi\n' >"$ZACOS9_MODPROBE_CONF"
printf 'appletalk\n' >"$ZACOS9_MODULES_LOAD_CONF"
"$helper" extension bluetooth off btusb
[ "$(cat "$ZACOS9_MODPROBE_CONF")" = "$(printf 'blacklist iwlwifi\nblacklist btusb')" ] ||
	fail "another extension's blacklist: $(cat "$ZACOS9_MODPROBE_CONF")"
"$helper" extension bluetooth on btusb
[ "$(cat "$ZACOS9_MODPROBE_CONF")" = "blacklist iwlwifi" ] ||
	fail "turning Bluetooth on took Wi-Fi's blacklist: $(cat "$ZACOS9_MODPROBE_CONF")"
[ "$(cat "$ZACOS9_MODULES_LOAD_CONF")" = "$(printf 'appletalk\nbtusb')" ] ||
	fail "modules-load.d with another entry: $(cat "$ZACOS9_MODULES_LOAD_CONF")"

# Module and key names that aren't plain module names are refused, and
# nothing is written for them.
before=$(cat "$ZACOS9_MODPROBE_CONF")
for bad in '../x' 'two words' '-rf' 'a*b' 'a;b' "$(printf 'x\ny')"; do
	! "$helper" extension bluetooth off "$bad" 2>/dev/null || fail "module name refused: $bad"
done
! "$helper" extension '../key' off btusb 2>/dev/null || fail "an odd extension key refused"
[ "$(cat "$ZACOS9_MODPROBE_CONF")" = "$before" ] || fail "a refused name wrote to the blacklist"

# A missing command or direction is refused too.
! "$helper" 2>/dev/null || fail "no command refused"
! "$helper" extension 2>/dev/null || fail "an extension command with no key refused"
! "$helper" extension bluetooth 2>/dev/null || fail "no direction refused"
! "$helper" extension bluetooth maybe btusb 2>/dev/null || fail "an unknown direction refused"
! "$helper" something 2>/dev/null || fail "an unknown command refused"

echo "ok: zacos9-modules-helper turns an extension's modules off and on for the next startup"

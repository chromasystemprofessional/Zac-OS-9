#!/bin/sh
# zacos9-accounts-helper's settings, in a scratch folder (no root needed:
# ZACOS9_ACCOUNTS_CONF and ZACOS9_LOCALE_GEN stand in for /etc).
set -eu
helper=$1
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
export ZACOS9_ACCOUNTS_CONF=$T ZACOS9_LOCALE_GEN=$T/locale.gen
fail() { echo "FAIL: $*" >&2; exit 1; }

"$helper" puppy someone on && [ -e "$T/puppy/someone" ] || fail "Puppy Mode kept"
"$helper" status | grep -qx "puppy someone" || fail "status lists it"
"$helper" puppy someone off && [ ! -e "$T/puppy/someone" ] || fail "Puppy Mode taken away"
! "$helper" puppy '../x' on 2>/dev/null || fail "odd user names refused"

printf 'Hello,\n family\001' | "$helper" welcome
[ "$(cat "$T/welcome")" = "Hello,  family" ] || fail "welcome on one line: $(cat "$T/welcome")"
printf '' | "$helper" welcome && [ ! -e "$T/welcome" ] || fail "an empty welcome removes it"

printf '# fr_FR.UTF-8 UTF-8\nen_US.UTF-8 UTF-8\n' >"$T/locale.gen"
"$helper" locale fr_FR.UTF-8 && "$helper" locale de_DE.UTF-8
[ "$(cat "$T/locale.gen")" = "$(printf 'fr_FR.UTF-8 UTF-8\nen_US.UTF-8 UTF-8\nde_DE.UTF-8 UTF-8')" ] ||
	fail "locale.gen: $(cat "$T/locale.gen")"
! "$helper" locale 'x;y' 2>/dev/null || fail "odd locale names refused"
! "$helper" locale xx_XX.UTF-8 2>/dev/null || fail "unsupported locales refused"
export ZACOS9_SYSTEMD_DIR=$T/systemd ZACOS9_RTC=$T/wakealarm ZACOS9_NO_SYSTEMCTL=1
export ZACOS9_ADJTIME=$T/adjtime # a hardware clock in UTC, whatever this computer's is
printf '0.0 0 0.0\n0\nUTC\n' >"$T/adjtime"
printf 'startup daily 07:30\nshutdown weekdays 23:00\n' | "$helper" schedule
grep -qx "OnCalendar=Mon..Fri \*-\*-\* 23:00:00" "$T/systemd/zacos9-shutdown.timer" || fail "the shut-down timer"
alarm=$(cat "$T/wakealarm")
[ "$(date -d "@$alarm" +%H:%M)" = 07:30 ] && [ "$alarm" -gt "$(date +%s)" ] || fail "the wake alarm: $alarm"
# A hardware clock in local time: the kernel reads the alarm as that clock's
# own time, so 07:30 there is 07:30 "UTC", in summer and winter alike.
printf '0.0 0 0.0\n0\nLOCAL\n' >"$T/adjtime"
for zone in America/New_York Asia/Kolkata Australia/Sydney; do
	TZ=$zone "$helper" wakealarm
	alarm=$(cat "$T/wakealarm")
	[ "$(date -u -d "@$alarm" +%H:%M)" = 07:30 ] || fail "the wake alarm on a local-time clock ($zone): $alarm"
done
printf '' | "$helper" schedule
[ ! -e "$T/systemd/zacos9-shutdown.timer" ] && [ "$(cat "$T/wakealarm")" = 0 ] || fail "the schedule turned off"
! printf 'startup daily 25:00\n' | "$helper" schedule 2>/dev/null || fail "odd times refused"
echo "ok: zacos9-accounts-helper keeps Puppy Mode, the welcome message, locales and Nap Time's schedule"

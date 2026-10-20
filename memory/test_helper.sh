#!/bin/sh
# zacos9-memory-helper's setting, in a scratch folder (no root needed:
# ZACOS9_ZRAM_CONF stands in for /etc/systemd/zram-generator.conf, and
# ZACOS9_NO_SYSTEMCTL leaves systemd alone).
set -eu
helper=$1
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
export ZACOS9_ZRAM_CONF=$T/systemd/zram-generator.conf ZACOS9_NO_SYSTEMCTL=1
fail() { echo "FAIL: $*" >&2; exit 1; }

"$helper" zram on 50
grep -qx "\[zram0\]" "$ZACOS9_ZRAM_CONF" || fail "a zram0 section"
grep -qx "zram-size = ram \* 50 / 100" "$ZACOS9_ZRAM_CONF" || fail "half of memory: $(cat "$ZACOS9_ZRAM_CONF")"
grep -qx "compression-algorithm = zstd" "$ZACOS9_ZRAM_CONF" || fail "zstd"
"$helper" zram on 100
grep -qx "zram-size = ram \* 100 / 100" "$ZACOS9_ZRAM_CONF" || fail "all of memory"
[ ! -e "$ZACOS9_ZRAM_CONF.tmp" ] || fail "no temporary file left"
! "$helper" zram on 30 2>/dev/null || fail "odd sizes refused"
! "$helper" zram on 2>/dev/null || fail "a size is needed"
"$helper" zram off
[ ! -e "$ZACOS9_ZRAM_CONF" ] || fail "off removes the setting"
"$helper" zram off # off when already off is fine
! "$helper" zram sideways 2>/dev/null || fail "unknown commands refused"
echo "ok: zacos9-memory-helper keeps Compressed Memory's setting"

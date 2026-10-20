#!/bin/sh
# UI test: the Memory control panel's Compressed Memory switch, through the
# real zacos9-memory-helper with a stand-in pkexec and a scratch
# zram-generator.conf (ZACOS9_NO_SYSTEMCTL: systemd is left alone).
# Run inside WSL, headless (no display needed):
#   sh tests/ui/memory.sh
# Expected:
#   /tmp/memory-1-off.png   Off; the pop-up grey; swap on disk reported
#   /tmp/memory-2-on.png    after "On": the setting written, "half of memory"
#   /tmp/memory-3-menu.png  the pop-up open
#   /tmp/memory-4-all.png   after "all of memory": zram-size = ram * 100 / 100
#   /tmp/memory-5-off.png   after "Off": the setting removed
# and "ok: ..." on stdout.
set -eu
cd "$(dirname "$0")/../.."
T=$(mktemp -d /tmp/zacos9-memory-XXXXXX)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/run" "$T/cfg/zacos9" "$T/bin"
printf '#!/bin/sh\nexec "$@"\n' >"$T/bin/pkexec"
chmod +x "$T/bin/pkexec"
# The window's outer frame: the first row below the menu bar with a run of
# 300 black pixels, and where it starts. The clicks are measured from it.
cat >"$T/frame.py" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
px = im.load()
for y in range(21, im.height):
    run = 0
    for x in range(im.width):
        run = run + 1 if px[x, y] == (0, 0, 0) else 0
        if run >= 300:
            print(x - run + 1, y)
            sys.exit(0)
sys.exit("no frame")
PY
cat >"$T/script.sh" <<SCRIPT
build/shell/zacos9-controlpanel memory & sleep 4
grim /tmp/memory-1-off.png
set -- \$(python3 "$T/frame.py" /tmp/memory-1-off.png)
build/shell/vptr home move \$((\$1 + 35)) \$((\$2 + 123)) click wait 1500         # On
grim /tmp/memory-2-on.png
cp "$T/zram.conf" "$T/after-on" 2>/dev/null || true
build/shell/vptr home move \$((\$1 + 205)) \$((\$2 + 149)) click wait 800         # the pop-up
grim /tmp/memory-3-menu.png
build/shell/vptr home move \$((\$1 + 205)) \$((\$2 + 181)) click wait 1500        # all of memory
grim /tmp/memory-4-all.png
cp "$T/zram.conf" "$T/after-all" 2>/dev/null || true
build/shell/vptr home move \$((\$1 + 91)) \$((\$2 + 123)) click wait 1500         # Off
grim /tmp/memory-5-off.png
[ -e "$T/zram.conf" ] && touch "$T/still-on" || true
kill \$PPID
SCRIPT
env -u WAYLAND_DISPLAY -u DISPLAY PATH="$T/bin:$PATH" XDG_RUNTIME_DIR="$T/run" HOME="$T" \
	XDG_CONFIG_HOME="$T/cfg" WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
	ZACOS9_STARTUP=0 ZACOS9_FINDER= ZACOS9_COLLAR= ZACOS9_INHIBIT= QT_QPA_PLATFORM=wayland \
	QT_STYLE_OVERRIDE= QT_WAYLAND_DISABLE_WINDOWDECORATION=1 \
	ZACOS9_ZRAM_CONF="$T/zram.conf" ZACOS9_NO_SYSTEMCTL=1 \
	ZACOS9_MEMORY_HELPER="$PWD/memory/zacos9-memory-helper" \
	timeout 60 build/compositor/zacos9-wm -s "sh $T/script.sh" >"$T/wm.log" 2>&1 || true
fail() { echo "FAIL: $*" >&2; exit 1; }
grep -qx "zram-size = ram \* 50 / 100" "$T/after-on" 2>/dev/null || fail "On writes half of memory"
grep -qx "zram-size = ram \* 100 / 100" "$T/after-all" 2>/dev/null || fail "the pop-up changes the size"
[ ! -e "$T/still-on" ] || fail "Off removes the setting"
echo "ok: the Memory panel turns Compressed Memory on, resizes it and turns it off"

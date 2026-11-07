#!/bin/sh
# zacos9-rotate-boot's GRUB rewrite, in a scratch folder: no root, no real
# /etc/default/grub or /sys, and a stand-in update-grub.
set -eu
helper=$1
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
conf=$T/desktop.conf
grub=$T/grub
drm=$T/drm
mkdir -p "$drm/card0-eDP-1" "$drm/card0-HDMI-A-1" "$drm/card1-DP-1" "$T/bin"
echo connected >"$drm/card0-eDP-1/status"
echo disconnected >"$drm/card0-HDMI-A-1/status"
echo connected >"$drm/card1-DP-1/status"
cat >"$T/bin/update-grub" <<EOF
#!/bin/sh
echo grub >>"$T/update.log"
EOF
chmod +x "$T/bin/update-grub"
PATH="$T/bin:$PATH"
export ZACOS9_ROTATE_CONF=$conf ZACOS9_ROTATE_GRUB=$grub ZACOS9_ROTATE_DRM_SYS=$drm
fail() { echo "FAIL: $*" >&2; exit 1; }
check_line() {
	grep -qxF "$1" "$grub" || fail "expected '$1' in $grub; it is:
$(cat "$grub")"
}

printf 'GRUB_DEFAULT=0\nGRUB_CMDLINE_LINUX_DEFAULT="quiet"\nGRUB_CMDLINE_LINUX="quiet splash"\n' >"$grub"
: >"$T/update.log"
printf 'rotation=90\n' >"$conf"
"$helper"
check_line 'GRUB_CMDLINE_LINUX="quiet splash fbcon=rotate:1 video=eDP-1:panel_orientation=left_side_up video=DP-1:panel_orientation=left_side_up"'
check_line 'GRUB_CMDLINE_LINUX_DEFAULT="quiet"'
[ -s "$T/update.log" ] || fail "update-grub was not run after the first change"

cp "$grub" "$T/grub.before"
: >"$T/update.log"
"$helper"
cmp -s "$grub" "$T/grub.before" || fail "a second run changed the file"
[ ! -s "$T/update.log" ] || fail "update-grub ran when nothing changed"

printf 'rotation=180\n' >"$conf"
"$helper"
check_line 'GRUB_CMDLINE_LINUX="quiet splash fbcon=rotate:2 video=eDP-1:panel_orientation=upside_down video=DP-1:panel_orientation=upside_down"'
printf 'rotation=270\n' >"$conf"
"$helper"
check_line 'GRUB_CMDLINE_LINUX="quiet splash fbcon=rotate:3 video=eDP-1:panel_orientation=right_side_up video=DP-1:panel_orientation=right_side_up"'
printf 'rotation="left"\n' >"$conf"
"$helper"
check_line 'GRUB_CMDLINE_LINUX="quiet splash fbcon=rotate:1 video=eDP-1:panel_orientation=left_side_up video=DP-1:panel_orientation=left_side_up"'

printf 'rotation=0\n' >"$conf"
"$helper"
check_line 'GRUB_CMDLINE_LINUX="quiet splash"'

# A screen named on its own (a hand-written file, or a single-screen one).
printf 'display.eDP-1.rotation=90\n' >"$conf"
"$helper"
check_line 'GRUB_CMDLINE_LINUX="quiet splash fbcon=rotate:1 video=eDP-1:panel_orientation=left_side_up video=DP-1:panel_orientation=left_side_up"'

# main-display names the screen whose rotation is the machine's.
printf 'main-display=eDP-1\ndisplay.eDP-1.rotation=270\n' >"$conf"
"$helper"
check_line 'GRUB_CMDLINE_LINUX="quiet splash fbcon=rotate:3 video=eDP-1:panel_orientation=right_side_up video=DP-1:panel_orientation=right_side_up"'

# An unknown value leaves the file alone.
printf 'rotation=45\n' >"$conf"
cp "$grub" "$T/grub.before"
"$helper" 2>/dev/null
cmp -s "$grub" "$T/grub.before" || fail "an unknown rotation changed the file"

# No rotation key at all: leave the file alone (it may not be ours).
printf '# nothing about rotation\n' >"$conf"
cp "$grub" "$T/grub.before"
"$helper"
cmp -s "$grub" "$T/grub.before" || fail "a file with no rotation changed the file"

# An unquoted GRUB_CMDLINE_LINUX is accepted and written back quoted.
printf 'GRUB_CMDLINE_LINUX=quiet\n' >"$grub"
printf 'rotation=90\n' >"$conf"
"$helper"
check_line 'GRUB_CMDLINE_LINUX="quiet fbcon=rotate:1 video=eDP-1:panel_orientation=left_side_up video=DP-1:panel_orientation=left_side_up"'

# No configuration at all: leave the file alone.
rm -f "$conf"
cp "$grub" "$T/grub.before"
"$helper"
cmp -s "$grub" "$T/grub.before" || fail "a missing configuration changed the file"

echo "ok: zacos9-rotate-boot puts the rotation on the kernel command line"

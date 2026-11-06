#!/bin/sh
# zacos9-fcitx-autostart starts Fcitx 5 only in a Japanese session, and
# gives it the Japanese config directory without touching other languages.
set -eu

script=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir "$tmp/bin"
cat >"$tmp/bin/fcitx5" <<'EOF'
#!/bin/sh
echo "started XDG_CONFIG_DIRS=${XDG_CONFIG_DIRS:-}" >>"$FCITX_LOG"
EOF
chmod +x "$tmp/bin/fcitx5"
export PATH="$tmp/bin:$PATH" FCITX_LOG="$tmp/log" XDG_CONFIG_DIRS=/etc/xdg
fail=0
check() {
	if [ "$1" = "$2" ]; then
		echo "ok: $3"
	else
		echo "FAIL: $3 (wanted '$2', got '$1')"
		fail=1
	fi
}

rm -f "$FCITX_LOG"
LANG=en_US.UTF-8 LC_ALL= LC_CTYPE= "${script}"
check "$([ -e "$FCITX_LOG" ] && cat "$FCITX_LOG")" "" "an English session does not start Fcitx"

LANG=ja_JP.UTF-8 LC_ALL= LC_CTYPE= "${script}"
check "$(cat "$FCITX_LOG" 2>/dev/null)" \
	"started XDG_CONFIG_DIRS=/usr/share/zacos9/japanese/xdg:/etc/xdg" \
	"a Japanese session starts Fcitx with the Japanese config directory"

rm -f "$FCITX_LOG"
LANG=ja_JP.UTF-8 LC_ALL=C "${script}"
check "$([ -e "$FCITX_LOG" ] && cat "$FCITX_LOG")" "" \
	"LC_ALL overrides a Japanese LANG"
exit $fail

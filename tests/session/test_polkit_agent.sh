#!/bin/sh
# zacos9-polkit-agent starts the session's polkit agent only when no
# other agent is already running, so LXpolkit or another desktop's agent
# does not produce a second-agent error.
set -eu

script=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir "$tmp/bin"

cat >"$tmp/bin/lxqt-policykit-agent" <<EOF
#!/bin/sh
echo started >"$tmp/started"
EOF
chmod +x "$tmp/bin/lxqt-policykit-agent"

# pgrep as the wrapper uses it: the last argument is the process name.
cat >"$tmp/bin/pgrep" <<'EOF'
#!/bin/sh
for last; do :; done
[ -f "$PGREP_RUNNING" ] && grep -qx "$last" "$PGREP_RUNNING"
EOF
chmod +x "$tmp/bin/pgrep"

export PATH="$tmp/bin:$PATH" PGREP_RUNNING="$tmp/running"
fail=0
check() {
	if [ "$1" = "$2" ]; then
		echo "ok: $3"
	else
		echo "FAIL: $3 (wanted '$2', got '$1')"
		fail=1
	fi
}

rm -f "$tmp/running" "$tmp/started"
"$script"
check "$([ -e "$tmp/started" ] && echo yes)" "yes" "starts the session agent when none is running"

echo lxpolkit >"$tmp/running"
rm -f "$tmp/started"
"$script"
check "$([ -e "$tmp/started" ] && echo yes)" "" "does not start a second agent when LXpolkit is running"

echo lxqt-policykit-agent >"$tmp/running"
rm -f "$tmp/started"
"$script"
check "$([ -e "$tmp/started" ] && echo yes)" "" "does not start when this agent is already running"

echo unrelated >"$tmp/running"
rm -f "$tmp/started"
"$script"
check "$([ -e "$tmp/started" ] && echo yes)" "yes" "an unrelated process does not block the agent"

exit $fail

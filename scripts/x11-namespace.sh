# Sourced by the run scripts after they set $self (their absolute path).
#
# Under WSLg, /tmp/.X11-unix is a read-only mount belonging to WSLg's own X
# server, so Xwayland can't create its socket there. Re-exec the calling
# script in a private mount namespace with a fresh sticky tmpfs on that path.
# Only this process tree sees it; WSLg is untouched. No-op on normal systems.
if [ -z "${ZACOS9_X11_NS:-}" ] && [ -d /tmp/.X11-unix ] && \
		! { [ -k /tmp/.X11-unix ] && [ -w /tmp/.X11-unix ]; }; then
	if [ "$(id -u)" = 0 ] && command -v unshare >/dev/null; then
		export ZACOS9_X11_NS=1
		exec unshare -m sh -c \
			'mount -t tmpfs -o mode=1777 tmpfs /tmp/.X11-unix && exec "$0" "$@"' \
			"$self" "$@"
	fi
	echo "warning: /tmp/.X11-unix unusable; X11 apps will not run" >&2
fi

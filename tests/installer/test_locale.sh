#!/bin/sh
# The installer's --locale argument: a language the live system doesn't have
# is refused before any disk is touched, and the argument alone isn't an
# install. No root and no device needed.
set -eu
helper=$1

if out=$(sh "$helper" --locale xx_XX.UTF-8 /dev/null 2>&1); then
	echo "accepted a language that doesn't exist" >&2
	exit 1
fi
case $out in
*"No such language"*) ;;
*) echo "wrong error for a bad language: $out" >&2; exit 1 ;;
esac

if out=$(sh "$helper" --locale it_IT.UTF-8 2>&1); then
	echo "accepted --locale without a device" >&2
	exit 1
fi
case $out in
*usage*) ;;
*) echo "wrong error for --locale alone: $out" >&2; exit 1 ;;
esac

echo "ok: the installer checks its language argument"

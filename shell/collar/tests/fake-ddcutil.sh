#!/bin/sh
# Stands in for ddcutil: one DDC/CI monitor on bus 7 (HDMI-A-1) whose
# brightness lives in $FAKE_DDC_STATE, and one that refuses DDC (DP-3).
echo "$*" >>"${FAKE_DDC_LOG:-/dev/null}"
case "$*" in
detect)
	cat <<'OUT'
Display 1
   I2C bus:  /dev/i2c-7
   DRM connector:           card1-HDMI-A-1
   EDID synopsis:
      Mfg id:               TST - Test
      Model:                Test Monitor

Invalid display
   I2C bus:  /dev/i2c-9
   DRM connector:           card1-DP-3
   EDID synopsis:
      Mfg id:               TST - Test
OUT
	;;
"--bus 7 --brief getvcp 10")
	echo "VCP 10 C $(cat "$FAKE_DDC_STATE") 100"
	;;
"--bus 7 setvcp 10 "*)
	if [ -n "$FAKE_DDC_FAIL" ]; then
		echo "Setting value failed for feature x10" >&2
		exit 1
	fi
	echo "$5" >"$FAKE_DDC_STATE"
	;;
*)
	echo "Unexpected ddcutil arguments: $*" >&2
	exit 2
	;;
esac

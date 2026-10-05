# Hardware quirks

Fixes for particular computers that the system has to apply, not the
desktop. They live in `hardware/`, ship in the `zacos9` package, and do
nothing on computers that don't need them.

## Haswell HDMI audio: the link position

**Symptom.** Sound through HDMI or DisplayPort stutters badly, while the
same sound through the built-in speakers or headphone jack plays cleanly.
PipeWire logs `hdmi:0p: snd_pcm_avail after recover: Broken pipe`, and the
kernel logs `Unstable LPIB (… >= …); disabling LPIB delay counting`.

**Cause.** The HDMI/DisplayPort audio controller in Intel's Haswell graphics
(PCI `8086:0a0c`, `0c0c`, `0d0c`) reports its playback position from a
position buffer that runs ahead of the sound actually sent.

- Measured on an iMac14,1 playing silence to a TV: the pointer mostly
  advanced at 48 kHz but leapt 500–2,400 frames within a millisecond or two.
  It overtook what PipeWire had written about 14 times a second.
- Each time, PipeWire treats it as an underrun and restarts the stream; the
  restart is the stutter. Larger PipeWire buffers (`api.alsa.headroom`) and
  interrupt-driven scheduling (`api.alsa.disable-tsched`) both still underran.
- The analog controller on the same machine had no underruns under the same
  load.

Linux already reads the link position (LPIB) instead on Broadwell's HDMI
controller, "can't use position buffer reliably" (`AZX_DCAPS_POSFIX_LPIB` in
the kernel's HD Audio driver). Haswell's controller doesn't get that
treatment upstream.

**Fix.** `zacos9-hda.conf` (in `/usr/lib/modprobe.d`) has modprobe load
`snd_hda_intel` through `zacos9-hda-load`. The script adds
`position_fix=1` (LPIB) for each Haswell HDMI controller and leaves every
other controller at the driver's default. The driver's options are per
controller, numbered in PCI order among HD Audio controllers, so on an
iMac14,1 it's `position_fix=1` (the HDMI controller is first).

- A `position_fix` set by the user, in `/etc/modprobe.d` or on the kernel
  command line, wins.
- The driver is always loaded, whatever the script finds.
- Takes effect the next time the driver loads, which in practice means a
  restart.

**Check** after restarting:

```sh
cat /sys/module/snd_hda_intel/parameters/position_fix   # 1,-1,-1,…
```

## ACPI interrupt storms on Macs

**Symptom.** A kernel thread, `irq/9-acpi`, uses most of a CPU all the time,
next to a busy `kworker/…kacpi_notify`. It runs at real-time priority,
above PipeWire's audio thread.

**Cause.** The firmware of some Macs leaves an ACPI general-purpose event
(GPE) firing nonstop under Linux. On the iMac14,1 it's GPE 0x06, the
graphics SCI (`\_SB.PCI0.IGPU.GSCI`), about 24,000 times a second from
start-up. Other models storm on other GPEs.

**Fix.** `zacos9-gpe-guard.timer` runs `zacos9-gpe-guard` 15 seconds after
start-up and every 5 minutes after that, since a storm can start later, when
a display is plugged in.

- The guard samples every GPE for 2 seconds. It masks any GPE that fired
  more than 1,000 times a second, by writing `mask` to
  `/sys/firmware/acpi/interrupts/gpeXX`, the run-time form of the
  `acpi_mask_gpe=` kernel option.
- Each GPE it masks is noted in the journal, for example
  `GPE 0x06 fired 24420 times a second; masked it.`
- It acts on Apple hardware only. Elsewhere a busy GPE may belong to the
  embedded controller, which the kernel looks after itself.

**Check:**

```sh
journalctl -u zacos9-gpe-guard
grep -H . /sys/firmware/acpi/interrupts/gpe*    # "masked" on the storming one
```

## Tests

`meson test -C build hardware-quirks` runs both scripts against a fake
`/sys` (`tests/hardware/test_quirks.py`).

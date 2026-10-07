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

## Haswell HDMI audio: freezes mid-stream

**Symptom.** HDMI sound stops in the middle of a video and doesn't come
back. Picking the built-in speakers brings sound back there at once, but
picking HDMI again stays silent; until now only a restart fixed it.

**What's happening.** The same Haswell HDMI controller stops moving its
playback position while the stream is still running. Seen on the iMac14,1
on 2026-10-07, under a once-a-second monitor of
`/proc/asound/card0/pcm3p/sub0/status`:

- The state stays `RUNNING` and PipeWire's sink stays `RUNNING`, but
  `hw_ptr` freezes (`avail_max: 0`); the TV is silent.
- The TV's audio information (ELD), the HDMI connector and the controller's
  power state all stay good, and HDA power saving was off. That rules out
  the TV, the cable and runtime suspend.
- It happened twice, 90 seconds apart, while Firefox was playing YouTube
  (48 kHz).
- Switching outputs doesn't help, because something else keeps the frozen
  device open: an interface sound's `pw-play`, for example, that never
  finishes.
- Closing and reopening the device does help (`pactl suspend-sink … 1`,
  then `0`). The position moves again and the TV plays, with no restart.
- No kernel message comes with a freeze, and the freezes come in bursts
  (often 40–80 seconds apart, then none for up to 40 minutes).
- Not the IOMMU: with the controller's IOMMU group switched to `identity`
  (untranslated), it ran 20 minutes clean and then froze again.

The cause isn't known yet; this is a recovery, not a fix.

**Recovery.** `zacos9-hdmi-watchdog.service`, a user service in
`/usr/lib/systemd/user` (enabled for every user), runs `zacos9-hdmi-watchdog`.

- Each second it reads the status of every playback PCM on a Haswell HDMI
  controller.
- If a running PCM's `hw_ptr` hasn't moved for 3 seconds (same trigger
  time, so the same stream), it suspends and resumes that controller's
  PipeWire sinks (`alsa_output.pci-0000_00_03.0.*`).
- That gives about 4 seconds of silence instead of silence until a restart.
- Each restart is noted in the journal, for example
  `HDMI audio on card0 stopped for 3s; restarting alsa_output.pci-0000_00_03.0.hdmi-stereo.`
- On computers without that controller it exits at once.

**Check** after restarting:

```sh
systemctl --user status zacos9-hdmi-watchdog
journalctl --user -u zacos9-hdmi-watchdog
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

`meson test -C build hardware-quirks` runs all three scripts against a fake
`/sys` and `/proc` (`tests/hardware/test_quirks.py`).

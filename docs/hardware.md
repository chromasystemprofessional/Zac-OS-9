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

## The Intel GPU on dual-GPU MacBook Pros

**Symptom.** A MacBookPro11,3 (and its dual-GPU kin) runs everything on the
NVIDIA GeForce GT 750M through nouveau: warm, short on battery, and slow,
since nouveau can't raise that chip's clocks. `lspci` shows no Intel
graphics at all.

**Cause.** Apple's firmware hides the Intel GPU unless macOS is starting,
and leaves the built-in screen on the NVIDIA one. Linux's own EFI stub asks
the firmware's "set OS" protocol too, but on this model too late: the
kernel log's `apple-properties: device path parse error -19` is the
firmware describing an Intel GPU (`0d26`) that isn't there.

**Fix.** `zacos9-intel-graphics` (in `/usr/libexec/zacos9/`):

- `setos.efi` (`hardware/intel-graphics/setos.c`, built with gnu-efi) has a
  firmware entry of its own, first in the boot order. It tells the firmware
  macOS is starting, then starts `\EFI\ZacOS9\shimx64.efi`; if it can't, the
  firmware goes on to ZacOS's own entry, next in the order.
- With both GPUs there, the kernel's gmux driver moves the screen between
  them (vga_switcheroo), but only while nothing has the NVIDIA GPU open. At
  start-up `zacos9-boot-handoff quit` ends the splash completely and runs
  `zacos9-intel-graphics switch`: it asks for `IGD` until the screen has
  moved, switches the NVIDIA GPU `OFF`, and writes the Intel GPU's card to
  `/run/zacos9/drm-devices`, which the login screen and the session pass to
  zacos9-wm as `WLR_DRM_DEVICES`.
- The external ports are wired to the NVIDIA GPU: an external display needs
  the usual (NVIDIA) start-up.

```sh
sudo /usr/libexec/zacos9/zacos9-intel-graphics enable --once   # the next start-up only
sudo /usr/libexec/zacos9/zacos9-intel-graphics enable          # from now on
sudo /usr/libexec/zacos9/zacos9-intel-graphics disable         # back to NVIDIA
/usr/libexec/zacos9/zacos9-intel-graphics status
journalctl -u plymouth-quit    # "the screen is on the Intel GPU (/dev/dri/card1)"
```

`--once` turns itself off during that start-up, so a start-up that went
wrong is followed by a normal one. Test: `intel-graphics`, `boot-handoff`.

## Two GPUs: which one renders

**Symptom.** With a second display on the motherboard's HDMI port, menus on
the big screen flicker while the pointer moves, and the session log has
`Failed to pick cursor plane format` once per motion.

**Cause.** The BIOS exposes the integrated GPU only when something is
plugged into that port, and then makes it the boot VGA device. wlroots
renders on the boot VGA device, whatever it is, and feeds every other card
by copying each frame across GPUs with a second renderer; that message comes
only from a card that is copied to (wlroots can't give it a hardware cursor),
so every pointer motion on it redraws the whole scanout and the menu
follows late.

**Fix.** `compositor/src/primarygpu.c`: before wlroots opens the cards,
zacos9-wm puts the one with the largest connected screen first in
`WLR_DRM_DEVICES`, so the copy goes to the smaller screens instead. The
kernel registers the boot VGA card first, which decides equal screens. A
`WLR_DRM_DEVICES` already set (the Mac's graphics switch, or a session's
own) is left alone, as is a nested session.

A screen wlroots cannot commit frames to at all (a card that finds no
swapchain format for it, say) used to be retried on every frame: the log
grew by a hundred megabytes in a few seconds and the session looked hung.
`compositor/src/output.c` now turns such a screen off once, with one line
saying so, and the session goes on drawing on its other screens.

**Check:**

```sh
zacos9-wm -d 2>&1 | grep WLR_DRM_DEVICES   # card1:card0, the 4K screen first
```

The same log keeps one `Failed to pick cursor plane format` line and then
`previous message repeated N more times` rather than a line per motion
(`compositor/src/logfilter.c`). Test: `primary-gpu`, `log-filter`.


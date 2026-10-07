"""Hardware quirk scripts against a fake /sys; run with the scripts' paths:
test_quirks.py hardware/zacos9-hda-load hardware/zacos9-gpe-guard \
    hardware/zacos9-hdmi-watchdog"""

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

root = Path(__file__).resolve().parents[2]
hdmi_watchdog = sys.argv.pop(3) if len(sys.argv) > 3 else str(root / "hardware/zacos9-hdmi-watchdog")
gpe_guard = sys.argv.pop(2) if len(sys.argv) > 2 else str(root / "hardware/zacos9-gpe-guard")
hda_load = sys.argv.pop(1) if len(sys.argv) > 1 else str(root / "hardware/zacos9-hda-load")

# The iMac14,1's PCI devices that matter here: host bridge, Iris Pro
# graphics, Haswell HDMI audio, Lynx Point analog audio.
IMAC14_1 = [
    ("0000:00:00.0", "0x060000", "0x8086", "0x0d04"),
    ("0000:00:02.0", "0x030000", "0x8086", "0x0d22"),
    ("0000:00:03.0", "0x040300", "0x8086", "0x0d0c"),
    ("0000:00:1b.0", "0x040300", "0x8086", "0x8c20"),
]


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def executable(path, text):
    write(path, text)
    path.chmod(0o755)


class HdaLoadTests(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.sysfs = self.dir / "sys"
        self.log = self.dir / "modprobe.log"
        self.config = self.dir / "modprobe.conf"
        self.config.write_text("options snd_pcsp index=-2\n")
        # modprobe -c prints the configuration; a load records its arguments.
        executable(self.dir / "modprobe", f"""#!/bin/sh
if [ "$1" = -c ]; then cat "{self.config}"; exit 0; fi
echo "$@" > "{self.log}"
""")

    def devices(self, devices):
        for address, cls, vendor, device in devices:
            base = self.sysfs / "bus/pci/devices" / address
            write(base / "class", cls + "\n")
            write(base / "vendor", vendor + "\n")
            write(base / "device", device + "\n")

    def load(self, *args):
        env = dict(os.environ, ZACOS9_SYSFS=str(self.sysfs),
                   ZACOS9_MODPROBE=str(self.dir / "modprobe"))
        subprocess.run(["sh", hda_load, *args], env=env, check=True)
        return self.log.read_text().split()

    def test_haswell_hdmi_reads_the_link_position(self):
        self.devices(IMAC14_1)
        self.assertEqual(self.load(),
                         ["--ignore-install", "snd_hda_intel", "position_fix=1"])

    def test_other_controllers_keep_the_default(self):
        self.devices([("0000:00:1f.3", "0x040300", "0x8086", "0xa348"),
                      ("0000:01:00.1", "0x040300", "0x10de", "0x10fa")])
        self.assertEqual(self.load(), ["--ignore-install", "snd_hda_intel"])

    def test_option_follows_the_controller_order(self):
        self.devices([("0000:00:01.0", "0x040300", "0x10de", "0x0e0f"),
                      ("0000:00:02.0", "0x030000", "0x8086", "0x0412"),
                      ("0000:00:03.0", "0x040300", "0x8086", "0x0c0c"),
                      ("0000:00:1b.0", "0x040300", "0x8086", "0x8c20")])
        self.assertEqual(self.load(),
                         ["--ignore-install", "snd_hda_intel", "position_fix=-1,1"])

    def test_users_own_position_fix_wins(self):
        self.devices(IMAC14_1)
        self.config.write_text("options snd_hda_intel model=auto position_fix=2\n")
        self.assertEqual(self.load(), ["--ignore-install", "snd_hda_intel"])

    def test_modprobe_command_line_options_pass_through(self):
        self.devices(IMAC14_1)
        self.assertEqual(self.load("power_save=1"),
                         ["--ignore-install", "snd_hda_intel", "power_save=1",
                          "position_fix=1"])

    def test_driver_loads_without_pci_information(self):
        self.assertEqual(self.load(), ["--ignore-install", "snd_hda_intel"])


def counter(count, masked=False):
    """A GPE counter as the kernel prints it."""
    return "%8u  EN STS enabled    %s\n" % (count, " masked  " if masked else " unmasked")


class GpeGuardTests(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.sysfs = self.dir / "sys"
        self.gpes = self.sysfs / "firmware/acpi/interrupts"
        write(self.sysfs / "class/dmi/id/sys_vendor", "Apple Inc.\n")
        write(self.gpes / "gpe_all", counter(0))
        write(self.gpes / "sci", counter(0))

    def guard(self, before, after):
        """Runs the guard; counters go from before to after during its wait."""
        for gpe, text in before.items():
            write(self.gpes / gpe, text)
        lines = "".join(f"printf '%s' '{text}' > '{self.gpes / gpe}'\n"
                        for gpe, text in after.items())
        executable(self.dir / "wait", "#!/bin/sh\n" + lines)
        env = dict(os.environ, ZACOS9_SYSFS=str(self.sysfs),
                   ZACOS9_GPE_SLEEP=str(self.dir / "wait"))
        return subprocess.run(["sh", gpe_guard], env=env, check=True,
                              capture_output=True, text=True).stdout

    def test_storm_is_masked(self):
        out = self.guard({"gpe06": counter(1000), "gpe17": counter(50)},
                         {"gpe06": counter(49000), "gpe17": counter(60)})
        self.assertEqual((self.gpes / "gpe06").read_text(), "mask\n")
        self.assertEqual((self.gpes / "gpe17").read_text(), counter(60))
        self.assertEqual(out, "GPE 0x06 fired 24000 times a second; masked it.\n")

    def test_busy_but_ordinary_events_are_left_alone(self):
        out = self.guard({"gpe1D": counter(0)}, {"gpe1D": counter(1900)})
        self.assertEqual((self.gpes / "gpe1D").read_text(), counter(1900))
        self.assertEqual(out, "")

    def test_masked_storm_is_not_masked_again(self):
        out = self.guard({"gpe06": counter(0, masked=True)},
                         {"gpe06": counter(90000, masked=True)})
        self.assertEqual((self.gpes / "gpe06").read_text(), counter(90000, masked=True))
        self.assertEqual(out, "")

    def test_only_on_macs(self):
        write(self.sysfs / "class/dmi/id/sys_vendor", "LENOVO\n")
        out = self.guard({"gpe6E": counter(0)}, {"gpe6E": counter(90000)})
        self.assertEqual((self.gpes / "gpe6E").read_text(), counter(0))
        self.assertEqual(out, "")


def pcm_status(hw, trigger="2077.837486253", state="RUNNING"):
    """A playback substream's status as ALSA prints it."""
    if state != "RUNNING":
        return state.lower() + "\n"
    return (f"state: {state}\nowner_pid   : 1299\ntrigger_time: {trigger}\n"
            f"tstamp      : 3133.197897247\ndelay       : 4192\n"
            f"avail       : 28576\navail_max   : 0\n-----\n"
            f"hw_ptr      : {hw}\nappl_ptr    : {hw + 4192}\n")


class HdmiWatchdogTests(unittest.TestCase):
    HDMI_SINK = "alsa_output.pci-0000_00_03.0.hdmi-stereo"

    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.sysfs = self.dir / "sys"
        self.procfs = self.dir / "proc"
        self.log = self.dir / "pactl.log"
        self.status = self.procfs / "asound/card0/pcm3p/sub0/status"
        self.analog = self.procfs / "asound/card1/pcm0p/sub0/status"
        self.card("card0", "0000:00:03.0", "0x0d0c")
        self.card("card1", "0000:00:1b.0", "0x8c20")
        executable(self.dir / "pactl", f"""#!/bin/sh
if [ "$1 $2 $3" = "list short sinks" ]; then
    printf '57\\talsa_output.pci-0000_00_1b.0.analog-stereo\\tPipeWire\\ts32le 2ch 48000Hz\\tRUNNING\\n'
    printf '78\\t{self.HDMI_SINK}\\tPipeWire\\ts32le 2ch 48000Hz\\tRUNNING\\n'
    exit 0
fi
echo "$@" >> "{self.log}"
""")

    def card(self, card, address, device):
        pci = self.sysfs / "bus/pci/devices" / address
        write(pci / "vendor", "0x8086\n")
        write(pci / "device", device + "\n")
        link = self.sysfs / "class/sound" / card / "device"
        link.parent.mkdir(parents=True, exist_ok=True)
        link.symlink_to(pci)

    def watch(self, samples, analog=None):
        """Runs one round per sample; each sleep moves to the next one."""
        write(self.status, samples[0])
        if analog:
            write(self.analog, analog[0])
        steps = "".join(
            f"[ $n = {i} ] && printf '%s' '{text}' > '{self.status}'\n"
            for i, text in enumerate(samples[1:], 1))
        if analog:
            steps += "".join(
                f"[ $n = {i} ] && printf '%s' '{text}' > '{self.analog}'\n"
                for i, text in enumerate(analog[1:], 1))
        counter = self.dir / "rounds"
        counter.write_text("0")
        # Only the one-second wait between rounds advances the samples.
        executable(self.dir / "sleep", f"""#!/bin/sh
[ "$1" = 1 ] || exit 0
n=$(($(cat '{counter}') + 1)); echo $n > '{counter}'
{steps}exit 0
""")
        env = dict(os.environ, ZACOS9_SYSFS=str(self.sysfs),
                   ZACOS9_PROCFS=str(self.procfs),
                   ZACOS9_PACTL=str(self.dir / "pactl"),
                   ZACOS9_SLEEP=str(self.dir / "sleep"),
                   ZACOS9_WATCHDOG_ROUNDS=str(len(samples)),
                   TMPDIR=str(self.dir))
        out = subprocess.run(["sh", hdmi_watchdog], env=env, check=True,
                             capture_output=True, text=True, timeout=30).stdout
        calls = self.log.read_text().splitlines() if self.log.exists() else []
        return out, calls

    def test_frozen_hdmi_is_restarted(self):
        out, calls = self.watch([pcm_status(1000), pcm_status(49000)] +
                                [pcm_status(49751)] * 4)
        self.assertEqual(calls, [f"suspend-sink {self.HDMI_SINK} 1",
                                 f"suspend-sink {self.HDMI_SINK} 0"])
        self.assertEqual(out, f"HDMI audio on card0 stopped for 3s; "
                              f"restarting {self.HDMI_SINK}.\n")

    def test_playing_hdmi_is_left_alone(self):
        out, calls = self.watch([pcm_status(48000 * n) for n in range(8)])
        self.assertEqual((out, calls), ("", []))

    def test_a_short_pause_is_not_a_stall(self):
        out, calls = self.watch([pcm_status(100), pcm_status(100), pcm_status(100),
                                 pcm_status(48100), pcm_status(48100),
                                 pcm_status(48100), pcm_status(96100)])
        self.assertEqual((out, calls), ("", []))

    def test_closed_or_restarted_streams_are_not_stalls(self):
        out, calls = self.watch([pcm_status(500), pcm_status(0, state="closed"),
                                 pcm_status(500), pcm_status(500, "2100.0"),
                                 pcm_status(500, "2100.0"), pcm_status(500, "2101.0"),
                                 pcm_status(500, "2101.0")])
        self.assertEqual((out, calls), ("", []))

    def test_frozen_analog_is_not_its_business(self):
        moving = [pcm_status(48000 * n) for n in range(6)]
        out, calls = self.watch(moving, analog=[pcm_status(7)] * 6)
        self.assertEqual((out, calls), ("", []))

    def test_restart_waits_for_another_full_stall(self):
        out, calls = self.watch([pcm_status(9)] * 6)
        self.assertEqual(len(calls), 2)
        self.log.unlink()
        out, calls = self.watch([pcm_status(9)] * 7)
        self.assertEqual(len(calls), 4)

    def test_other_computers_exit_at_once(self):
        (self.sysfs / "class/sound/card0/device").unlink()
        out, calls = self.watch([pcm_status(9)] * 6)
        self.assertEqual((out, calls), ("", []))
        self.assertEqual((self.dir / "rounds").read_text(), "0")


if __name__ == "__main__":
    unittest.main()

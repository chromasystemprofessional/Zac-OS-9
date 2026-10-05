"""Hardware quirk scripts against a fake /sys; run with the scripts' paths:
test_quirks.py hardware/zacos9-hda-load hardware/zacos9-gpe-guard"""

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

root = Path(__file__).resolve().parents[2]
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


if __name__ == "__main__":
    unittest.main()

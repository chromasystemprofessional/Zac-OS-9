#!/usr/bin/python3
"""The installer's "next to what's on it" (issue #42), end to end against
stand-ins: UDisks (tests/disksetup/fake_udisks.py: EFI, then Windows
filling a 128 GiB drive), lsblk, and zacos9-install (its sizes; the
arguments an install gets are recorded). Choose the disk, see ZacOS
planned beside Windows, drag the line between them down, Install:
zacos9-install --beside must shrink Windows to where the line was left
and put ZacOS from there to the drive's last MiB.

    test_beside.py ZACOS9-WM ZACOS9-INSTALLER VPTR
"""
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "disksetup"))
from stage import EXT4, GiB, MiB, NTFS, Stage, skip_unless_ready  # noqa: E402

skip_unless_ready()
compositor, installer, vptr = (Path(a).resolve() for a in sys.argv[1:4])
FAT = (0xFF, 0xCC, 0x66)
FACE = (0xDD, 0xDD, 0xDD)  # the window, GRAY(0xD)
MAP_TOP, BTN = 172, (424 + 40, 404 + 10)  # installer.cpp: the map's top, Install's middle

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    args = root / "install-args"
    (root / "bin").mkdir()
    lsblk = root / "bin" / "lsblk"
    lsblk.write_text("""#!/bin/sh
case "$*" in
*NAME,TYPE,SIZE,FSTYPE,MODEL,PKNAME,LABEL*)
	echo 'NAME="sdz" TYPE="disk" SIZE="128G" FSTYPE="" MODEL="Fake Disk 128" PKNAME="" LABEL=""'
	echo 'NAME="sdz1" TYPE="part" SIZE="100M" FSTYPE="vfat" MODEL="" PKNAME="sdz" LABEL=""'
	echo 'NAME="sdz2" TYPE="part" SIZE="127.9G" FSTYPE="ntfs" MODEL="" PKNAME="sdz" LABEL="Windows"'
	;;
esac
""")
    helper = root / "zacos9-install"
    helper.write_text(f"""#!/bin/sh
case "$1" in
--min-size) echo "min-size {20 * GiB}" ;;
--detect) ;;
*) echo "$*" >{args}; echo done ;;
esac
""")
    for f in (lsblk, helper):
        f.chmod(0o755)
    import os
    env = {"ZACOS9_INSTALL_HELPER": str(helper), "ZACOS9_FIRMWARE_EFI": directory,
           "PATH": f"{root / 'bin'}:{os.environ['PATH']}"}
    with Stage(compositor, [installer], env, vptr) as stage:
        # The window's face, clicked so it has the keys; Continue (Return),
        # then the disk (Down): Windows has things on it, so "next to what's
        # on it" is already chosen and ZacOS planned.
        fx0, fy0, fx1, fy1 = stage.look(FACE)
        stage.pointer("home", "move", (fx0 + fx1) // 2, (fy0 + fy1) // 2, "click", "wait", 300)
        # (A new virtual keyboard's first key is lost: Escape, which does nothing here.)
        stage.pointer("key", "Escape", "wait", 300, "key", "Return", "wait", 1500, "key", "Down", "wait", 1500)
        nx0, ny0, nx1, ny1 = stage.look(NTFS)
        zx0, zy0, zx1, zy1 = stage.look(EXT4)
        assert zy0 > ny1, "ZacOS is planned after Windows"
        fat = stage.look(FAT)
        origin = (fat[0] - 1 - 16, fat[1] - 1 - MAP_TOP)
        # The line between them (black, under Windows's colour), 40 down.
        stage.pointer("home", "move", (nx0 + nx1) // 2, ny1 + 1, "down", "move", 0, 40, "wait", 300, "up", "wait", 500)
        _, _, _, moved = stage.look(NTFS)
        assert moved > ny1 + 20, "the line moved down: Windows keeps more"
        stage.pointer("home", "move", origin[0] + BTN[0], origin[1] + BTN[1], "click")
        end = time.monotonic() + 10
        while time.monotonic() < end and not args.exists():
            time.sleep(0.2)
    words = args.read_text().split()

print(" ".join(words))
assert words[:3] == ["--beside", "/dev/sdz", "Zacintosh"] and words[3] == "HD", "the disk and its name"
steps = words[4:]
assert steps[0:2] == ["shrink", "/dev/sdz2"] and steps[3] == "zacos" and len(steps) == 6, \
    "Windows made smaller, then ZacOS's partition; the EFI System partition there already"
windows = int(steps[2])
offset, size = int(steps[4]), int(steps[5])
assert 20 * GiB <= windows < 128 * GiB - 102 * MiB, "Windows smaller, never below its files"
assert offset == 101 * MiB + windows, "ZacOS starts where Windows now ends"
assert offset + size == 128 * GiB - MiB and size >= 16000 * MiB, "to the drive's last MiB, 16 GB at least"
print("ok: the installer puts ZacOS next to Windows, where the line was dragged")

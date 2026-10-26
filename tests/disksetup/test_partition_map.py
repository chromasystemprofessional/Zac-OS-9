#!/usr/bin/python3
"""Disk Setup's partition map, end to end against a stand-in UDisks
(fake_udisks.py: a made-up drive, EFI then Windows filling it): choose
Windows, drag the line under it up, make a partition in the free space,
name it, Apply. UDisks must be asked for the files' resize, then the
partition's, then the new partition right after it to the drive's end.

    test_partition_map.py ZACOS9-WM ZACOS9-DISKSETUP VPTR
"""
import sys
import tempfile
from pathlib import Path

from stage import GiB, MiB, NTFS, Stage, skip_unless_ready

skip_unless_ready()
compositor, disksetup, vptr = (Path(a).resolve() for a in sys.argv[1:4])

with tempfile.TemporaryDirectory() as directory:
    helper = Path(directory) / "zacos9-install"
    helper.write_text('#!/bin/sh\necho "min-size %d"\n' % (20 * GiB))
    helper.chmod(0o755)
    with Stage(compositor, [disksetup, "--partition"], {"ZACOS9_INSTALL_HELPER": str(helper)}, vptr) as stage:
        x0, y0, x1, y1 = stage.look(NTFS)
        cx = (x0 + x1) // 2
        # Windows chosen: its files are measured (the stand-in helper says 20 GiB).
        stage.pointer("home", "move", cx, (y0 + y1) // 2, "click", "wait", 800)
        # The line under it is at the frame's bottom, less 2 (inside the frame).
        stage.pointer("home", "move", cx, y1 - 1, "down", "move", 0, -(y1 - y0) // 4, "wait", 300, "up", "wait", 500)
        _, _, _, w1 = stage.look(NTFS)
        assert w1 < y1 - 20, "dragging the line didn't make Windows smaller"
        # The free space under it, then +, a name, Apply, and yes.
        stage.pointer("home", "move", cx, w1 + 12, "click", "wait", 300)
        stage.pointer("home", "move", x0 + 13, y1 + 19, "click", "wait", 400)
        stage.pointer("type", "Zacintosh", "wait", 300, "key", "Return", "wait", 1200, "key", "Return", "wait", 2000)
        steps = stage.udisks()

print("\n".join(steps))
windows_start = 101 * MiB
fs, part, create = steps[0].split(), steps[1].split(), steps[2].split()
size = int(fs[2])
assert fs[:2] == ["sdz2", "filesystem-resize"] and part[:2] == ["sdz2", "partition-resize"], \
    "Windows's files first, then its partition"
assert int(part[2]) == size and 20 * GiB <= size < 128 * GiB - 102 * MiB and size % MiB == 0, \
    "the same size for both, no smaller than its files, a whole MiB"
assert create[0] == "create" and int(create[1]) == windows_start + size, \
    "the new partition starts where Windows now ends"
assert int(create[1]) + int(create[2]) == 128 * GiB - MiB, "and runs to the drive's last MiB"
assert create[4:] == ["ext4", "Zacintosh"], "ZacOS's format and the name typed"
print("ok: the partition map drags, adds and applies")

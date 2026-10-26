#!/usr/bin/python3
"""Disk Setup's partition map, end to end against a stand-in UDisks
(fake_udisks.py: a made-up drive, EFI then Windows filling it): choose
Windows, drag the line under it up, make a partition in the free space,
name it, Apply. UDisks must be asked for the files' resize, then the
partition's, then the new partition right after it to the drive's end.

    test_partition_map.py ZACOS9-WM ZACOS9-DISKSETUP VPTR
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("SKIP: Pillow is needed to find the map on screen")
    sys.exit(77)
if not shutil.which("grim") or not shutil.which("dbus-daemon"):
    print("SKIP: grim and dbus-daemon are needed")
    sys.exit(77)

compositor, disksetup, vptr = (Path(a).resolve() for a in sys.argv[1:4])
fake = Path(__file__).with_name("fake_udisks.py")
MiB = 1024 * 1024
GiB = 1024 * MiB
WINDOWS = (0x66, 0xCC, 0xCC)  # NTFS's colour in the map
FREE = (0xFF, 0xFF, 0xFF)


def stop(process):
    if process and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()


def block(path, colour):
    """The bounding box of `colour` on the screen, or None."""
    image = Image.open(path).convert("RGB")
    xs, ys = [], []
    pixels = image.load()
    for y in range(0, image.height, 2):
        for x in range(0, image.width, 2):
            if pixels[x, y] == colour:
                xs.append(x)
                ys.append(y)
    if len(xs) < 200:
        return None
    return min(xs), min(ys), max(xs), max(ys)


with tempfile.TemporaryDirectory(prefix="zacos9-partition-map-") as directory:
    root = Path(directory)
    log = root / "udisks.log"
    log.touch()
    helper = root / "zacos9-install"
    helper.write_text('#!/bin/sh\necho "min-size %d"\n' % (20 * GiB))
    helper.chmod(0o755)
    bus = subprocess.run(["dbus-daemon", "--session", "--fork", "--print-address=1", "--print-pid=1"],
                         capture_output=True, text=True, check=True).stdout.split()
    address, bus_pid = bus[0], int(bus[1])
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_CONFIG_HOME=directory, HOME=directory,
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
               ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="", ZACOS9_COLLAR="",
               QT_QPA_PLATFORM="wayland", DBUS_SESSION_BUS_ADDRESS=address,
               DBUS_SYSTEM_BUS_ADDRESS=address, ZACOS9_INSTALL_HELPER=str(helper))
    env.pop("WAYLAND_DISPLAY", None)
    udisks = server = None
    try:
        udisks = subprocess.Popen([sys.executable, "-I", fake, log], env=env, stdout=subprocess.PIPE, text=True)
        assert udisks.stdout.readline().strip() == "ready", "the stand-in UDisks didn't start"
        server = subprocess.Popen([compositor, "-s", f"{disksetup} --partition"], env=env,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline and not [p for p in root.glob("wayland-*") if not p.name.endswith(".lock")]:
            time.sleep(0.05)
        env["WAYLAND_DISPLAY"] = [p for p in root.glob("wayland-*") if not p.name.endswith(".lock")][0].name
        shot = root / "shot.png"

        def look(colour, timeout=15):
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                subprocess.run(["grim", shot], env=env, check=True)
                box = block(shot, colour)
                if box:
                    return box
                time.sleep(0.3)
            raise AssertionError(f"{colour} never showed on screen")

        def pointer(*words):
            subprocess.run([vptr, *map(str, words)], env=env, check=True, timeout=30)

        x0, y0, x1, y1 = look(WINDOWS)
        cx = (x0 + x1) // 2
        # Windows chosen: its files are measured (the stand-in helper says 20 GiB).
        pointer("home", "move", cx, (y0 + y1) // 2, "click", "wait", 800)
        # The line under it is at the frame's bottom, less 2 (inside the frame).
        line = y1 - 1
        pointer("home", "move", cx, line, "down", "move", 0, -(y1 - y0) // 4, "wait", 300, "up", "wait", 500)
        _, _, _, w1 = look(WINDOWS)
        assert w1 < y1 - 20, "dragging the line didn't make Windows smaller"
        # The free space under it, then +, a name, Apply, and yes.
        pointer("home", "move", cx, w1 + 12, "click", "wait", 300)
        pointer("home", "move", x0 + 13, y1 + 19, "click", "wait", 400)
        pointer("type", "Zacintosh", "wait", 300, "key", "Return", "wait", 1200, "key", "Return", "wait", 2000)

        steps = log.read_text().split("\n")
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
    finally:
        stop(server)
        stop(udisks)
        os.kill(bus_pid, 15)

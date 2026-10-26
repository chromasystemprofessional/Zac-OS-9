"""A headless desktop with a stand-in UDisks (fake_udisks.py), for tests of
programs that partition: Disk Setup's map and the installer.

    with Stage(compositor, [program, args...], {"ZACOS9_INSTALL_HELPER": ...}) as stage:
        x0, y0, x1, y1 = stage.look(NTFS)
        stage.pointer("home", "move", x0, y0, "click")
        stage.udisks()  # what UDisks was asked to do, a line a step
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
    Image = None

MiB = 1024 * 1024
GiB = 1024 * MiB
# Formats' colours in the partition map (diskmap.cpp).
NTFS = (0x66, 0xCC, 0xCC)
EXT4 = (0x99, 0x99, 0xFF)


def skip_unless_ready():
    if Image is None:
        print("SKIP: Pillow is needed to find things on screen")
        sys.exit(77)
    if not shutil.which("grim") or not shutil.which("dbus-daemon"):
        print("SKIP: grim and dbus-daemon are needed")
        sys.exit(77)


def _stop(process):
    if process and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()


class Stage:
    def __init__(self, compositor, command, env=None, vptr=None):
        self.compositor = Path(compositor)
        self.command = " ".join(str(c) for c in command)
        self.extra = env or {}
        self.vptr = vptr

    def __enter__(self):
        self.dir = tempfile.TemporaryDirectory(prefix="zacos9-stage-")
        self.root = Path(self.dir.name)
        self.log = self.root / "udisks.log"
        self.log.touch()
        bus = subprocess.run(["dbus-daemon", "--session", "--fork", "--print-address=1", "--print-pid=1"],
                             capture_output=True, text=True, check=True).stdout.split()
        self.bus_pid = int(bus[1])
        self.env = dict(os.environ, XDG_RUNTIME_DIR=str(self.root), XDG_CONFIG_HOME=str(self.root),
                        HOME=str(self.root), WLR_BACKENDS="headless", WLR_RENDERER="pixman",
                        WLR_HEADLESS_OUTPUTS="1", ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="",
                        ZACOS9_COLLAR="", QT_QPA_PLATFORM="wayland", DBUS_SESSION_BUS_ADDRESS=bus[0],
                        DBUS_SYSTEM_BUS_ADDRESS=bus[0], **self.extra)
        self.env.pop("WAYLAND_DISPLAY", None)
        self.udisks_process = self.server = None
        self.udisks_process = subprocess.Popen(
            [sys.executable, "-I", Path(__file__).with_name("fake_udisks.py"), self.log],
            env=self.env, stdout=subprocess.PIPE, text=True)
        assert self.udisks_process.stdout.readline().strip() == "ready", "the stand-in UDisks didn't start"
        self.server = subprocess.Popen([self.compositor, "-s", self.command], env=self.env,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            sockets = [p for p in self.root.glob("wayland-*") if not p.name.endswith(".lock")]
            if sockets:
                self.env["WAYLAND_DISPLAY"] = sockets[0].name
                return self
            time.sleep(0.05)
        raise AssertionError("the compositor didn't start")

    def __exit__(self, *_):
        _stop(self.server)
        _stop(self.udisks_process)
        os.kill(self.bus_pid, 15)
        self.dir.cleanup()

    def look(self, colour, timeout=15):
        """The bounding box of `colour` on screen, once it's there."""
        shot = self.root / "shot.png"
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            subprocess.run(["grim", shot], env=self.env, check=True)
            image = Image.open(shot).convert("RGB")
            pixels = image.load()
            xs, ys = [], []
            for y in range(0, image.height, 2):
                for x in range(0, image.width, 2):
                    if pixels[x, y] == colour:
                        xs.append(x)
                        ys.append(y)
            if len(xs) >= 200:
                return min(xs), min(ys), max(xs), max(ys)
            time.sleep(0.3)
        if os.environ.get("ZACOS9_TEST_SHOT"):
            shutil.copy(shot, os.environ["ZACOS9_TEST_SHOT"])  # to see what was there instead
        raise AssertionError(f"{colour} never showed on screen")

    def pointer(self, *words):
        subprocess.run([self.vptr, *map(str, words)], env=self.env, check=True, timeout=30)

    def udisks(self):
        return [line for line in self.log.read_text().split("\n") if line]

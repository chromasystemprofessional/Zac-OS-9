"""zacos9-intel-graphics against stand-ins for /sys/class/drm, the kernel's
vga_switcheroo file and the settings file: switching only when asked, once
only for --once, asking the kernel until the screen has moved, switching the
NVIDIA GPU off, telling the compositor which GPU to use; status.

usage: test_intel_graphics.py SCRIPT
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

script = sys.argv[1]
fails = 0


def check(ok, what):
    global fails
    print(("ok    " if ok else "FAIL  ") + what)
    fails += not ok


def rig(d, mode, switcheroo):
    root = Path(d)
    drm = root / "drm"
    for card, driver in (("card0", "nouveau"), ("card1", "i915")):
        (root / "drivers" / driver).mkdir(parents=True, exist_ok=True)
        (drm / card / "device").mkdir(parents=True)
        os.symlink(root / "drivers" / driver, drm / card / "device" / "driver")
    (drm / "card1-eDP-1").mkdir()
    (drm / "card1-eDP-1" / "status").write_text("connected\n")
    (drm / "card0-eDP-1").mkdir()
    (drm / "card0-eDP-1" / "status").write_text("disconnected\n")
    if mode:
        (root / "gpu").write_text(mode + "\n")
    (root / "switch").write_text(switcheroo)
    env = dict(os.environ, ZACOS9_GPU_CONF=str(root / "gpu"), ZACOS9_RUN_DIR=str(root / "run"),
               ZACOS9_SWITCHEROO=str(root / "switch"), ZACOS9_DRM_SYSFS=str(drm), ZACOS9_SWITCH_TRIES="3",
               ZACOS9_EFIBOOTMGR="true")
    return root, env


def run(env, *args):
    return subprocess.run([script, *args], env=env, capture_output=True, text=True, timeout=30)


NVIDIA_ON = "0:DIS:+:Pwr:0000:01:00.0\n1:IGD: :Pwr:0000:00:02.0\n"
MOVED = "0:DIS: :Pwr:0000:01:00.0\n1:IGD:+:Pwr:0000:00:02.0\n"

with tempfile.TemporaryDirectory() as d:
    root, env = rig(d, None, NVIDIA_ON)
    r = run(env, "switch")
    check(r.returncode == 0 and (root / "switch").read_text() == NVIDIA_ON and not (root / "run").exists(),
          "not asked for: nothing switched")

with tempfile.TemporaryDirectory() as d:
    root, env = rig(d, "intel-once", MOVED)
    r = run(env, "switch")
    check(r.returncode == 0 and (root / "switch").read_text() == MOVED + "OFF\n",
          "once the screen is on the Intel GPU, the NVIDIA one is switched off")
    check((root / "run" / "drm-devices").read_text() == "/dev/dri/card1\n",
          "the compositor is told to use the Intel GPU's card")
    check(not (root / "gpu").exists(), "--once: the setting is gone, the next start-up is the usual one")

with tempfile.TemporaryDirectory() as d:
    root, env = rig(d, "intel", NVIDIA_ON)
    r = run(env, "switch")
    check(r.returncode == 1 and (root / "switch").read_text() == NVIDIA_ON + "IGD\nIGD\nIGD\n",
          "the kernel is asked again while it refuses, then it gives up")
    check(not (root / "run").exists(), "a screen that didn't move: the compositor isn't told anything")
    check((root / "gpu").read_text() == "intel\n", "intel (not once): the setting stays")

with tempfile.TemporaryDirectory() as d:
    root, env = rig(d, "intel", "0:DIS:+:Pwr:0000:01:00.0\n")
    r = run(env, "switch")
    check(r.returncode == 1 and "setos.efi" in r.stderr and (root / "switch").read_text().count("IGD") == 0,
          "no Intel GPU (setos.efi didn't run): nothing is tried")

with tempfile.TemporaryDirectory() as d:
    root, env = rig(d, "intel", NVIDIA_ON)
    out = run(env, "status").stdout
    check("Intel GPU: visible (/dev/dri/card1)" in out and "card1: i915, built-in screen: yes" in out and
          "card0: nouveau, built-in screen: no" in out and "Mode: intel" in out, "status")

sys.exit(1 if fails else 0)

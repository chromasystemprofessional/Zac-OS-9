"""zacos9-classic and the startup disk chosen in Startup Disk: without a
choice, hard disks before CDs; a chosen hard disk attached first; a chosen
CD started from as a CD (--bootdriver -62); a choice that's gone ignored.

usage: test_classic.py LAUNCHER
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

launcher = sys.argv[1]
failures = 0


def check(ok, what):
    global failures
    print(("ok    " if ok else "FAIL  ") + what)
    failures += not ok


def hfs(path, size=512 * 1024):
    data = bytearray(size)
    data[1024:1026] = b"BD"
    path.write_bytes(bytes(data))


with tempfile.TemporaryDirectory() as d:
    root = Path(d)
    home, classic, emulators = root / "home", root / "home" / "Classic", root / "emulators"
    classic.mkdir(parents=True)
    emulators.mkdir()
    # An Old World ROM by its checksum, two hard disks and a CD.
    (classic / "rom").write_bytes(bytes.fromhex("9630c68b") + bytes(4 * 1024 * 1024 - 4))
    hd, other, cd = classic / "A.dsk", classic / "B.dsk", classic / "Mac OS 9.iso"
    for p in (hd, other, cd):
        hfs(p)
    sheep = emulators / "SheepShaver"
    sheep.write_text("#!/bin/sh\nfor a in \"$@\"; do echo \"$a\"; done\n")
    sheep.chmod(0o755)
    conf = home / ".config" / "zacos9" / "classic.conf"
    conf.parent.mkdir(parents=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith(("XDG_", "ZACOS9_"))}
    env.update(HOME=str(home), ZACOS9_EMULATORS=str(emulators))

    def run(*args, env=env):
        r = subprocess.run([launcher, *args], env=env, capture_output=True, text=True, timeout=30)
        return r.returncode, r.stdout.splitlines()

    def disks(argv):
        return [argv[i + 1] for i, a in enumerate(argv) if a == "--disk"]

    code, out = run()
    check(code == 0 and sorted(disks(out)[:2]) == [str(hd), str(other)] and disks(out)[2] == "*" + str(cd) and
          "--bootdriver" not in out, "no choice: hard disks first, the CD read-only")

    conf.write_text(f"other=kept\nstartup-disk={other}\n")
    code, out = run()
    check(disks(out)[0] == str(other) and len(disks(out)) == 3 and "--bootdriver" not in out,
          "a chosen hard disk is attached first")
    check(f"startup: {other}" in run("--check")[1], "--check names it")

    conf.write_text(f"startup-disk={cd}\n")
    code, out = run()
    check(disks(out)[-1] == "*" + str(cd) and out[out.index("--bootdriver") + 1] == "-62",
          "a chosen CD: started from as a CD")

    conf.write_text(f"startup-disk={classic / 'gone.dsk'}\n")
    code, out = run()
    check(code == 0 and sorted(disks(out)[:2]) == [str(hd), str(other)] and disks(out)[2] == "*" + str(cd) and
          not any(l.startswith("startup:") for l in run("--check")[1]),
          "a chosen disk that's gone is ignored")

    # The screen: full screen at the desktop's resolution when the
    # compositor's output list is readable, the 800x600 window otherwise.
    def screen(argv):
        return argv[argv.index("--screen") + 1]

    check(screen(run()[1]) == "win/800/600", "no desktop to read: the 800x600 window")
    runtime = root / "runtime"
    runtime.mkdir()
    outputs = runtime / "zacos9-outputs-wayland-0"
    desktop = {**env, "XDG_RUNTIME_DIR": str(runtime), "WAYLAND_DISPLAY": "wayland-0"}

    outputs.write_text("output HDMI-A-1 1920x1080 scale 1 nested 0 x 0 y 0 main 1 rotate 0\n")
    check(screen(run(env=desktop)[1]) == "dga/1920/1080", "full screen at the main screen's resolution")
    check("screen: dga/1920/1080" in run("--check", env=desktop)[1], "--check names the screen mode")
    check(screen(run("--windowed", env=desktop)[1]) == "win/800/600", "--windowed keeps the window")

    outputs.write_text("output eDP-1 2880x1800 scale 2 nested 0 x 0 y 0 main 1 rotate 90\n")
    check(screen(run(env=desktop)[1]) == "dga/900/1440",
          "the scale divides the mode and a rotated screen turns the size")

    (home / ".config" / "zacos9" / "classic.conf").write_text("screen=window\n")
    check(screen(run(env=desktop)[1]) == "win/800/600", "classic.conf's screen=window is followed")
    (home / ".config" / "zacos9" / "classic.conf").unlink()
    check(screen(run("--fullscreen")[1]) == "dga/800/600",
          "--fullscreen without a desktop still goes full screen")

sys.exit(1 if failures else 0)

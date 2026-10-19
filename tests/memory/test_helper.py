"""zacos9-memory-helper with stand-ins for every program it runs: status
from meminfo and swaps, virtual memory made, resized and taken back (and
refused when memory or the disk has no room), /etc/fstab's line kept
single, other swap left alone, earlyoom on and off, and the RAM Disk on,
saved at shut down, restored at startup and off.

usage: test_helper.py HELPER
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

helper = sys.argv[1]
failures = 0
MB = 1024 * 1024


def check(ok, what):
    global failures
    print(("ok    " if ok else "FAIL  ") + what)
    failures += not ok


FAKES = {
    # Each logs its name and arguments; some keep a little state.
    "swapon": "", "swapoff": "", "mkswap": "", "dd": "", "btrfs": "",
    "earlyoom": "",
    "fallocate": '/usr/bin/truncate -s "$2" "$3"',
    "findmnt": "echo ext4",
    "systemctl": '[ "$1" = is-enabled ] && /usr/bin/cat "$STATE/enabled" 2>/dev/null; true',
    "mountpoint": '[ -e "$STATE/mounted" ]',
    "mount": '/usr/bin/touch "$STATE/mounted"',
    "umount": '[ -e "$STATE/busy" ] && exit 1; /usr/bin/rm -f "$STATE/mounted"',
    "tar": '[ "$1" = -cpf ] && echo saved > "$2"; true',
}

with tempfile.TemporaryDirectory() as d:
    base = Path(d)
    root, proc, bin_, state = base / "root", base / "proc", base / "bin", base / "state"
    for p in (root / "etc", proc, bin_, state):
        p.mkdir(parents=True)
    log = base / "log"
    for name, body in FAKES.items():
        script = bin_ / name
        script.write_text(f'#!/bin/sh\necho "{name} $*" >> "$LOG"\n{body}\n')
        script.chmod(0o755)
    (root / "etc/fstab").write_text("UUID=1234 / ext4 errors=remount-ro 0 1\n")

    def proc_files(available_mb=8000, swaps=()):
        (proc / "meminfo").write_text(
            f"MemTotal:       16000000 kB\nMemAvailable:   {available_mb * 1024} kB\n"
            "Buffers:          100000 kB\nCached:          2000000 kB\nShmem:            100000 kB\n")
        lines = ["Filename\tType\tSize\tUsed\tPriority"]
        lines += [f"{name}\tfile\t{size_mb * 1024}\t{used_mb * 1024}\t-2" for name, size_mb, used_mb in swaps]
        (proc / "swaps").write_text("\n".join(lines) + "\n")

    env = {"PATH": str(bin_), "LOG": str(log), "STATE": str(state),
           "ZACOS9_MEMORY_ROOT": str(root), "ZACOS9_MEMORY_PROC": str(proc),
           "ZACOS9_MEMORY_UID": "1234"}

    def run(*args):
        log.write_text("")
        r = subprocess.run([sys.executable, "-I", helper, *args], env=env,
                           capture_output=True, text=True, timeout=20)
        return r.returncode, r.stdout, r.stderr.strip(), log.read_text()

    def facts():
        _, out, _, _ = run("status")
        found = {}
        for line in out.splitlines():
            fields = line.split("\t")
            found.setdefault(fields[0], []).append(fields[1:])
        return found

    # ---- status
    proc_files(swaps=[("/dev/zram0", 4096, 10)])
    (state / "enabled").write_text("enabled\n")
    f = facts()
    check(f["memory"][0] == [str(16000000 * 1024), str(8000 * MB), str(2000000 * 1024)],
          "status reports memory, what's available and the disk cache")
    check(f["swapfile"][0] == ["0", "0", "no"] and f["other-swap"][0] == ["/dev/zram0", str(4096 * MB), str(10 * MB)],
          "status tells no swap file from other swap it leaves alone")
    check(f["guard"][0] == ["yes", "yes"] and f["ramdisk"][0][0] == "no",
          "status reports earlyoom and the RAM Disk")

    # ---- virtual memory on
    code, _, err, calls = run("swap", "1024")
    fstab = (root / "etc/fstab").read_text()
    check(code == 0 and (root / "swapfile").stat().st_size == 1024 * MB and
          f"mkswap {root}/swapfile" in calls and f"swapon {root}/swapfile" in calls,
          f"swap 1024 makes a 1 GB swap file and turns it on {err}")
    check(fstab.count("/swapfile none swap sw 0 0") == 1 and "UUID=1234 / ext4" in fstab,
          "the swap file gets one fstab line, other lines kept")
    proc_files(swaps=[("/swapfile", 1024, 100), ("/dev/zram0", 4096, 10)])
    code, _, _, calls = run("swap", "2048")
    check(code == 0 and f"swapoff {root}/swapfile" in calls and
          (root / "swapfile").stat().st_size == 2048 * MB and
          (root / "etc/fstab").read_text().count("/swapfile") == 1,
          "resizing takes the swap file back, remakes it and keeps one fstab line")
    check("zram" not in calls, "other swap is never touched")

    # ---- refusals
    proc_files(available_mb=300, swaps=[("/swapfile", 2048, 1500)])
    code, _, err, calls = run("swap", "off")
    check(code == 1 and "Quit some programs" in err and "swapoff" not in calls,
          "turning virtual memory off is refused while memory has no room for it")
    code, _, err, _ = run("swap", str(100 * 1000 * 1000))
    check(code == 1 and "isn't room on the disk" in err, "a swap file too big for the disk is refused")
    code, _, err, _ = run("swap", "100")
    check(code == 1 and "256 MB" in err, "a swap file under 256 MB is refused")

    # ---- virtual memory off
    proc_files(swaps=[("/swapfile", 2048, 100)])
    code, _, _, calls = run("swap", "off")
    check(code == 0 and f"swapoff {root}/swapfile" in calls and not (root / "swapfile").exists() and
          "/swapfile" not in (root / "etc/fstab").read_text() and "UUID=1234" in (root / "etc/fstab").read_text(),
          "swap off takes it back, removes the file and its fstab line only")

    # ---- earlyoom
    code, _, _, calls = run("guard", "off")
    check(code == 0 and "systemctl disable --now earlyoom" in calls, "guard off disables earlyoom")
    code, _, _, calls = run("guard", "on")
    check(code == 0 and "systemctl enable --now earlyoom" in calls, "guard on enables earlyoom")
    (bin_ / "earlyoom").rename(base / "earlyoom")
    code, _, err, _ = run("guard", "on")
    check(code == 1 and "isn't installed" in err, "guard on says so when earlyoom isn't installed")
    (base / "earlyoom").rename(bin_ / "earlyoom")

    # ---- the RAM Disk
    proc_files()
    code, _, err, calls = run("ramdisk", "512", "save")
    conf = (root / "etc/zacos9/memory.conf").read_text()
    check(code == 0 and "size=512M,uid=1234,gid=1234,mode=0700" in calls and
          "ramdisk=on" in conf and "ramdisk-mb=512" in conf and "ramdisk-save=yes" in conf and
          "ramdisk-owner=1234" in conf, f"ramdisk 512 save mounts it for its owner and keeps the setting {err}")
    code, _, err, _ = run("ramdisk", "9000", "save")
    check(code == 1 and "half of memory" in err, "a RAM Disk over half of memory is refused")
    code, _, _, calls = run("ramdisk", "256", "save")
    check(code == 0 and "mount -o remount,size=256M" in calls, "resizing a mounted RAM Disk remounts it")
    code, _, _, calls = run("ramdisk", "stop")
    check(code == 0 and "tar -cpf" in calls and (root / "var/lib/zacos9/ramdisk.tar").exists(),
          "at shut down the RAM Disk is saved")
    (state / "mounted").unlink()
    code, _, _, calls = run("ramdisk", "start")
    check(code == 0 and "mount -t tmpfs -o size=256M,uid=1234" in calls and "tar -xpf" in calls,
          "at startup it is made again and what was saved put back")
    (state / "busy").touch()
    code, _, err, _ = run("ramdisk", "off")
    check(code == 1 and "in use" in err, "a RAM Disk in use isn't taken away")
    (state / "busy").unlink()
    code, _, _, calls = run("ramdisk", "off")
    check(code == 0 and "umount" in calls and "ramdisk=off" in (root / "etc/zacos9/memory.conf").read_text() and
          not (root / "var/lib/zacos9/ramdisk.tar").exists(),
          "ramdisk off unmounts it and forgets what was saved")
    code, _, _, calls = run("ramdisk", "start")
    check(code == 0 and "mount" not in calls, "at startup a RAM Disk that's off stays off")

    code, _, err, _ = run("swap")
    check(code == 1 and "usage" in err, "a wrong command line says how to use it")

sys.exit(1 if failures else 0)

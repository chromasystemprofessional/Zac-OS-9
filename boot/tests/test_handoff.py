#!/usr/bin/python3
"""zacos9-boot-handoff with a fake plymouth, systemctl and /proc."""
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

script = sys.argv[1]
fails = 0


def check(ok, what):
    global fails
    print(("ok:   " if ok else "FAIL: ") + what)
    fails += not ok


class Rig:
    def __init__(self, d):
        self.d = Path(d)
        self.log = self.d / "log"
        self.log.write_text("")
        self.up = self.d / "up"
        self.up.touch()
        self.proc = self.d / "proc"
        self.proc.mkdir()
        self.marker = self.d / "run" / "handoff"
        for name, status in (("plymouth", f"[ -e {self.up} ] || exit 1\n"
                              f'[ "$1" = quit ] && rm -f {self.up}\n'
                              f'[ "$1" = deactivate ] && [ -e {self.d}/no-deactivate ] && exit 1\nexit 0'),
                             ("systemctl", f'[ -e {self.d}/no-systemctl ] && exit 1\nexit 0')):
            f = self.d / name
            f.write_text(f'#!/bin/sh\necho "{name} $*" >> {self.log}\n{status}\n')
            f.chmod(0o755)
        (self.d / "uptime").write_text("28.51 100.00\n")
        self.env = dict(os.environ, ZACOS9_HANDOFF_PLYMOUTH=str(self.d / "plymouth"),
                        ZACOS9_HANDOFF_SYSTEMCTL=str(self.d / "systemctl"),
                        ZACOS9_HANDOFF_PROC=str(self.proc), ZACOS9_HANDOFF_FILE=str(self.marker),
                        ZACOS9_HANDOFF_UPTIME=str(self.d / "uptime"),
                        ZACOS9_HANDOFF_SECONDS="2", ZACOS9_HANDOFF_SETTLE="0.1")

    def process(self, pid, comm, fds=()):
        p = self.proc / str(pid)
        (p / "fd").mkdir(parents=True)
        (p / "comm").write_text(comm + "\n")
        for i, target in enumerate(fds):
            os.symlink(target, p / "fd" / str(i))

    def run(self, mode, **kw):
        return subprocess.run([script, mode], env=self.env, timeout=10, **kw)

    def calls(self):
        return [l for l in self.log.read_text().splitlines() if "--ping" not in l]


with tempfile.TemporaryDirectory() as d:
    r = Rig(d)
    check(r.run("quit").returncode == 0, "quit succeeds")
    check(r.calls() == ["plymouth deactivate",
                        "systemctl start --no-block zacos9-boot-handoff.service"],
          "quit leaves the splash up and starts the watcher")
    check(r.marker.read_text().strip() == "28.51", "quit notes the time for the compositor")
    r.log.write_text("")
    check(r.run("wait").returncode == 0 and r.calls() == [],
          "wait doesn't hold up the login once handed over")

    r.process(1, "systemd")
    r.process(400, "plymouthd", ["/dev/dri/card0", "/dev/tty1"])
    t = time.time()
    r.process(900, "zacos9-wm", ["/dev/null", "/dev/dri/card0"])
    r.log.write_text("")
    check(r.run("watch").returncode == 0, "watch succeeds")
    check(r.calls() == ["plymouth quit --retain-splash"],
          "the desktop taking the display ends the splash, keeping its frame")

with tempfile.TemporaryDirectory() as d:
    r = Rig(d)
    r.process(400, "plymouthd", ["/dev/dri/card0"])
    r.process(500, "tuigreet", ["/dev/tty1"])
    r.run("watch")
    check(r.calls() == ["plymouth quit"], "a text login clears the splash")

with tempfile.TemporaryDirectory() as d:
    r = Rig(d)
    r.process(400, "plymouthd", ["/dev/dri/card0"])
    t = time.time()
    r.run("watch")
    check(r.calls() == ["plymouth quit"] and time.time() - t >= 0.9,
          "nothing taking over: the splash still goes, after the time limit")

with tempfile.TemporaryDirectory() as d:
    r = Rig(d)
    r.up.unlink()
    check(r.run("quit").returncode == 0 and r.calls() == [] and not r.marker.exists(),
          "no splash: quit does nothing")
    r.run("wait")
    check(r.calls() == ["plymouth --wait"],
          "no handover: wait waits for the splash as usual")

for failing in ("no-deactivate", "no-systemctl"):
    with tempfile.TemporaryDirectory() as d:
        r = Rig(d)
        (r.d / failing).touch()
        r.run("quit")
        check(r.calls()[-1] == "plymouth quit" and not r.marker.exists(),
              f"quit just ends the splash if it can't hand over ({failing})")

sys.exit(1 if fails else 0)

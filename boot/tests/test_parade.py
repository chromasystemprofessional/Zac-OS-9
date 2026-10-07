#!/usr/bin/python3
"""zacos9-parade: records extensions in the order their modules load, sends
each to the splash once it answers, and skips modules that aren't curated."""
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

helper, catalog = sys.argv[1], Path(sys.argv[2]).read_text()
keys = re.findall(r'^\t\{ "([a-z0-9-]+)"', catalog, re.M)
index = {k: i for i, k in enumerate(keys)}
fails = 0


def check(ok, what):
    global fails
    print(("ok:   " if ok else "FAIL: ") + what)
    fails += not ok


def wait_for(cond, timeout=3.0):
    end = time.time() + timeout
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.05)
    return cond()


with tempfile.TemporaryDirectory() as d:
    d = Path(d)
    modules, record, log, up = d / "modules", d / "parade", d / "plymouth.log", d / "up"
    plymouth = d / "plymouth"
    plymouth.write_text(f"""#!/bin/sh
echo "$*" >> {log}
[ -e {up} ]
""")
    plymouth.chmod(0o755)
    log.write_text("")
    # Newest first, as the kernel lists them: i915 loaded before the sound.
    modules.write_text("snd_hda_intel 61440 2 - Live 0x0\ncrc32c 16384 0 - Live 0x0\n"
                       "i915 4194304 30 - Live 0x0\n")
    env = dict(os.environ, ZACOS9_PARADE_MODULES=str(modules), ZACOS9_PARADE_FILE=str(record),
               ZACOS9_PARADE_PLYMOUTH=str(plymouth), ZACOS9_PARADE_SECONDS="3",
               ZACOS9_PARADE_ANYTIME="1")
    proc = subprocess.Popen([helper], env=env)

    lines = lambda: record.read_text().split() if record.exists() else []
    updates = lambda: [l for l in log.read_text().splitlines() if l.startswith("update")]
    check(wait_for(lambda: lines() == ["intel-graphics", "built-in-sound"]),
          "records curated extensions in load order, skipping helper modules")
    time.sleep(0.3)
    check(updates() == [] and "--ping" in log.read_text(),
          "waits for the splash to answer before sending")

    up.touch()
    want = [f"update --status=zacos9-ext:{index['intel-graphics']}",
            f"update --status=zacos9-ext:{index['built-in-sound']}"]
    check(wait_for(lambda: updates() == want), "sends each icon once the splash is up")

    modules.write_text("btusb 81920 0 - Live 0x0\nsnd_hda_codec_hdmi 1 0 - Live 0x0\n" +
                       modules.read_text())
    check(wait_for(lambda: lines() == ["intel-graphics", "built-in-sound", "hdmi-sound",
                                       "bluetooth"]),
          "adds extensions as more modules load")
    check(wait_for(lambda: len(updates()) == 4 and
                   updates()[3] == f"update --status=zacos9-ext:{index['bluetooth']}"),
          "sends the new ones too, without repeating")
    check(proc.wait(timeout=5) == 0, "stops on its own")

    # Long after boot (installing the package starts it): does nothing.
    record.unlink()
    env.pop("ZACOS9_PARADE_ANYTIME")
    uptime = float(Path("/proc/uptime").read_text().split()[0])
    if uptime > 300:
        check(subprocess.run([helper], env=env, timeout=5).returncode == 0 and
              not record.exists(), "does nothing when started long after boot")

sys.exit(1 if fails else 0)

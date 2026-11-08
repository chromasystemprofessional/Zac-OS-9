"""zacos9-bloodhound: the app shell asks the Sniffer for its Find window
(shell/bloodhound/zacos9-bloodhound). A stand-in zacos9-finder-ctl records
the arguments, so the test doesn't need a running desktop.

usage: test_launch.py ZACOS9-BLOODHOUND
"""
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

script = Path(sys.argv[1]).resolve()


def fake_ctl(path, log):
    path.write_text("#!/bin/sh\nprintf '%s\\n' \"$@\" > " + shlex.quote(str(log)) + "\n")
    path.chmod(0o755)


def check(program, env, log):
    subprocess.run([str(program)], env=env, check=True, timeout=10)
    assert log.read_text().splitlines() == ["command", "find"], log.read_text()


with tempfile.TemporaryDirectory(prefix="zacos9-bloodhound-") as directory:
    root = Path(directory)

    # The configured helper, as a service or a test may set it.
    log = root / "args"
    fake = root / "zacos9-finder-ctl"
    fake_ctl(fake, log)
    env = dict(os.environ, ZACOS9_FINDER_CTL=str(fake))
    check(script, env, log)

    # Beside the program, as the package installs both in /usr/bin.
    beside = root / "bin"
    beside.mkdir()
    copied = beside / "zacos9-bloodhound"
    shutil.copy2(script, copied)
    log.unlink()
    fake_ctl(beside / "zacos9-finder-ctl", log)
    env = dict(os.environ, PATH="/usr/bin:/bin")
    env.pop("ZACOS9_FINDER_CTL", None)
    check(copied, env, log)

    # In the build tree, one folder over (build/shell/finderctl).
    tree = root / "build" / "shell"
    (tree / "bloodhound").mkdir(parents=True)
    (tree / "finderctl").mkdir()
    built = tree / "bloodhound" / "zacos9-bloodhound"
    shutil.copy2(script, built)
    log.unlink()
    fake_ctl(tree / "finderctl" / "zacos9-finder-ctl", log)
    env = dict(os.environ, PATH="/usr/bin:/bin")
    env.pop("ZACOS9_FINDER_CTL", None)
    check(built, env, log)

print("ok: Bloodhound asks the Sniffer for its Find window")

"""zacos9-classic --open: path mapping, one session, request and ack.

usage: test_open.py LAUNCHER
"""
import os
import shutil
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
    home, classic, emulators, runtime = root / "home", root / "home" / "Classic", root / "emulators", root / "run"
    for folder in (classic, emulators, runtime, home / "Apps"):
        folder.mkdir(parents=True)
    (classic / "rom").write_bytes(bytes.fromhex("9630c68b") + bytes(4 * 1024 * 1024 - 4))
    hfs(classic / "A.dsk")
    sheep = emulators / "SheepShaver"
    sheep.write_text("#!/bin/sh\nsleep 0.2\nexit 0\n")
    sheep.chmod(0o755)
    app = home / "Apps" / "Test App"
    app.write_text("an application")
    (root / "outside.App").write_text("an application")

    env = {k: v for k, v in os.environ.items() if not k.startswith(("XDG_", "ZACOS9_"))}
    env.update(
        HOME=str(home),
        ZACOS9_CLASSIC_DIR=str(classic),
        ZACOS9_EMULATORS=str(emulators),
        XDG_RUNTIME_DIR=str(runtime),
    )

    def run(*args):
        result = subprocess.run([launcher, *args], env=env, capture_output=True,
                                text=True, timeout=30)
        return result.returncode, result.stdout.splitlines(), result.stderr

    def request_text(number):
        path = home / "zacos9-classic" / "requests" / f"request.{number}.req"
        return path.read_text() if path.exists() else ""

    code, out, err = run("--check", "--open", str(app))
    check(code == 0 and f"open: {app} -> Unix:Apps:Test App" in out,
          "--check maps a home app to the Unix volume")
    check(not (home / "zacos9-classic").exists(), "--check writes no request")

    code, out, err = run("--open", str(app))
    check(code == 0 and request_text(1) ==
          "version=1\nid=1\npath=Apps:Test App\n",
          "a new session queues request 1 with the colon-separated path")

    # An existing session is owned by another process; ack files stand in for
    # Classic Open Helper here.
    shutil.rmtree(home / "zacos9-classic")
    (home / "zacos9-classic" / "requests").mkdir(parents=True)
    (home / "zacos9-classic" / "acks").mkdir(parents=True)
    (home / "zacos9-classic" / "sequence").write_text("0\n")
    (runtime / "zacos9-classic").mkdir(parents=True, exist_ok=True)
    sleeper = subprocess.Popen(["sleep", "30"])
    try:
        (runtime / "zacos9-classic" / "session.pid").write_text(f"{sleeper.pid}\n")
        (home / "zacos9-classic" / "acks" / "ack-1").write_text(
            "version=1\nid=1\nstatus=ok\ncode=0\n")
        code, out, err = run("--open", str(app))
        check(code == 0 and request_text(1) ==
              "version=1\nid=1\npath=Apps:Test App\n",
              "a running session receives the first request and its ack")
        (home / "zacos9-classic" / "acks" / "ack-2").write_text(
            "version=1\nid=2\nstatus=ok\ncode=0\n")
        code, out, err = run("--open", str(app))
        check(code == 0 and request_text(2) ==
              "version=1\nid=2\npath=Apps:Test App\n",
              "the next request gets a new name and sequence number")
        code, out, err = run("--open", str(app))
        check(code == 2 and "did not open" in err,
              "a missing acknowledgement fails instead of pretending success")
    finally:
        sleeper.terminate()
        sleeper.wait()

    code, out, err = run("--open", str(root / "outside.App"))
    check(code == 2 and "home folder" in err, "an app outside the home folder is refused")
    code, out, err = run("--open", str(home / "Apps" / "Missing"))
    check(code == 2 and "not found" in err, "a missing app is refused")

sys.exit(1 if failures else 0)

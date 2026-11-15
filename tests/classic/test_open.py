"""zacos9-classic --open: path mapping, one session, a queue of app requests.

usage: test_open.py LAUNCHER
"""
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import threading
import time
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


def request_id(path):
    for line in path.read_text().splitlines():
        if line.startswith("id="):
            return line[3:]
    return ""


def answer_requests(requests, acks, status="ok", code="0"):
    """Stand in for the emulator bridge: acknowledge and consume each request."""
    stop = threading.Event()
    delivered = []
    seen = set()

    def worker():
        while not stop.is_set():
            for path in sorted(requests.glob("request.*.req")):
                if path in seen:
                    continue
                seen.add(path)
                request = request_id(path)
                if request:
                    delivered.append(request)
                    (acks / f"ack-{request}").write_text(
                        f"version=1\nid={request}\nstatus={status}\ncode={code}\n")
                    path.unlink(missing_ok=True)
            time.sleep(0.02)

    thread = threading.Thread(target=worker, daemon=True)
    thread.start()
    return stop, delivered


def reset_queue(home):
    root = home / "zacos9-classic"
    shutil.rmtree(root, ignore_errors=True)
    (root / "requests").mkdir(parents=True)
    (root / "acks").mkdir()
    (root / "sequence").write_text("0\n")


with tempfile.TemporaryDirectory() as d:
    root = Path(d)
    home, classic, emulators, runtime = root / "home", root / "home" / "Classic", root / "emulators", root / "run"
    for folder in (classic, emulators, runtime, home / "Apps"):
        folder.mkdir(parents=True)
    (classic / "rom").write_bytes(bytes.fromhex("9630c68b") + bytes(4 * 1024 * 1024 - 4))
    hfs(classic / "A.dsk")
    sheep = emulators / "SheepShaver"
    record = root / "emulator-queue"
    sheep.write_text(
        "#!/bin/sh\n"
        f"printf '%s\\n' \"$ZACOS9_CLASSIC_QUEUE\" > {shlex.quote(str(record))}\n"
        "sleep 0.2\n"
        "exit 0\n")
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

    requests = home / "zacos9-classic" / "requests"
    acks = home / "zacos9-classic" / "acks"

    def request_text(number):
        path = requests / f"request.{number}.req"
        return path.read_text() if path.exists() else ""

    code, out, err = run("--check", "--open", str(app))
    check(code == 0 and f"open: {app} -> Unix:Apps:Test App" in out,
          "--check maps a home app to the Unix volume")
    check(not (home / "zacos9-classic").exists(), "--check writes no request")

    code, out, err = run("--open", str(app))
    check(code == 0 and request_text(1) ==
          "version=1\nid=1\npath=Apps:Test App\n",
          "a new session queues request 1 with the colon-separated path")
    check(record.exists() and record.read_text().strip() == str(home / "zacos9-classic"),
          "the emulator receives the queue path")

    # A running session accepts further requests. The bridge stand-in
    # acknowledges and removes each one, as the patched emulator does.
    reset_queue(home)
    (runtime / "zacos9-classic").mkdir(parents=True, exist_ok=True)
    sleeper = subprocess.Popen(["sleep", "30"])
    try:
        (runtime / "zacos9-classic" / "session.pid").write_text(f"{sleeper.pid}\n")
        stop, delivered = answer_requests(requests, acks)
        try:
            code, out, err = run("--open", str(app))
            check(code == 0 and delivered == ["1"] and (acks / "ack-1").exists(),
                  "a running session receives and acknowledges request 1")
            code, out, err = run("--open", str(app))
            check(code == 0 and delivered == ["1", "2"] and (acks / "ack-2").exists(),
                  "a second open in the same session receives request 2")
        finally:
            stop.set()

        reset_queue(home)
        stop, delivered = answer_requests(requests, acks, "error", "-50")
        try:
            code, out, err = run("--open", str(app))
            check(code == 2 and "refused" in err and "-50" in err,
                  "a guest error is reported instead of success")
        finally:
            stop.set()

        reset_queue(home)
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

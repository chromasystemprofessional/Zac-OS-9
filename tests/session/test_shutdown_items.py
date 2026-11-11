#!/usr/bin/env python3
"""Where Log Out goes: session/zacos9-shutdown-items.

A Wayland session asks its own compositor to quit (the display manager
then starts the next greeter, as it does when the session process exits);
only without one does it fall back to logind. The fake compositor is a
copy of sleep named zacos9-wm, as /proc/PID/comm must say.
"""
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import tempfile


helper = pathlib.Path(sys.argv[1])


def fixture(root):
    data = root / "data"
    items = data / "zacos9" / "Shutdown Items"
    items.mkdir(parents=True)
    log = root / "run.log"
    for name, body in (
        ("02-second", f"#!/bin/sh\necho second >> '{log}'\n"),
        ("01-first", f"#!/bin/sh\necho first >> '{log}'\n"),
        ("03-failed", "#!/bin/sh\nexit 1\n"),
    ):
        item = items / name
        item.write_text(body)
        item.chmod(0o700)
    (items / "not-executable").write_text("ignored")
    stub = root / "bin"
    stub.mkdir()
    loginctl = stub / "loginctl"
    loginctl.write_text(f"#!/bin/sh\nprintf '%s\\n' \"$@\" > '{root / 'logout.args'}'\n")
    loginctl.chmod(0o700)
    return data, log, stub


def env_for(root, data, stub):
    env = os.environ | {
        "HOME": str(root),
        "XDG_DATA_HOME": str(data),
        "PATH": f"{stub}:{os.environ['PATH']}",
    }
    for name in ("WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "ZACOS9_LOGOUT_COMMAND"):
        env.pop(name, None)
    return env


with tempfile.TemporaryDirectory(prefix="zacos9-shutdown-") as temp:
    root = pathlib.Path(temp)
    data, log, stub = fixture(root)
    env = env_for(root, data, stub) | {"XDG_SESSION_ID": "123abc"}

    # No compositor pid file: end the logind session by its id.
    result = subprocess.run(["/bin/sh", str(helper)], env=env, capture_output=True,
                            text=True, timeout=15)
    assert result.returncode == 0, result.stderr
    assert log.read_text().splitlines() == ["first", "second"]
    assert "03-failed" in result.stderr
    assert (root / "logout.args").read_text().splitlines() == ["terminate-session", "123abc"]

    # And without an id, loginctl's empty argument means this session.
    (root / "logout.args").unlink()
    env.pop("XDG_SESSION_ID")
    result = subprocess.run(["/bin/sh", str(helper)], env=env, capture_output=True,
                            text=True, timeout=15)
    assert result.returncode == 0, result.stderr
    assert (root / "logout.args").read_text().splitlines() == ["terminate-session", ""]
    print("ok: without a compositor, the login session ends through logind by id or by caller")

    # A compositor pid file: ask that process to quit; never kill the
    # whole logind session, which takes SDDM's helper with it.
    runtime = root / "runtime"
    runtime.mkdir()
    wm = stub / "zacos9-wm"
    shutil.copy2("/bin/sleep", wm)
    wm.chmod(0o700)
    compositor = subprocess.Popen([str(wm), "30"], env=env)
    (runtime / "zacos9-wm-wayland-1.pid").write_text(f"{compositor.pid}\n")
    (root / "logout.args").unlink()
    (root / "run.log").unlink()
    env |= {"XDG_RUNTIME_DIR": str(runtime), "WAYLAND_DISPLAY": "wayland-1"}
    result = subprocess.run(["/bin/sh", str(helper)], env=env, capture_output=True,
                            text=True, timeout=15)
    assert result.returncode == 0, result.stderr
    compositor.wait(timeout=5)
    assert compositor.returncode == -signal.SIGTERM, compositor.returncode
    assert not (root / "logout.args").exists(), "logind must not be asked to end the session"
    assert log.read_text().splitlines() == ["first", "second"]
    print("ok: with a compositor, Log Out asks it to quit and leaves logind alone")

print("ok: executable shutdown items run in order before the login session ends")

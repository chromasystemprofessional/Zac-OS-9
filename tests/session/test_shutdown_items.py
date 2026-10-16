#!/usr/bin/env python3
import os
import pathlib
import subprocess
import sys
import tempfile


helper = pathlib.Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix="zacos9-shutdown-") as temp:
    root = pathlib.Path(temp)
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

    stub_bin = root / "bin"
    stub_bin.mkdir()
    loginctl = stub_bin / "loginctl"
    loginctl.write_text(f"#!/bin/sh\necho \"$*\" > '{root / 'logout.log'}'\n")
    loginctl.chmod(0o700)
    env = os.environ | {
        "HOME": str(root),
        "XDG_DATA_HOME": str(data),
        "XDG_SESSION_ID": "123abc",
        "PATH": f"{stub_bin}:{os.environ['PATH']}",
    }
    result = subprocess.run(["/bin/sh", str(helper)], env=env, capture_output=True,
                            text=True, timeout=10)
    assert result.returncode == 0, result.stderr
    assert log.read_text().splitlines() == ["first", "second"]
    assert (root / "logout.log").read_text().strip() == "terminate-session 123abc"
    assert "03-failed" in result.stderr

print("ok: executable shutdown items run in order before the login session ends")

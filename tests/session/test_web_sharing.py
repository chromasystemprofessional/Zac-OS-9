#!/usr/bin/env python3
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

script = Path(__import__("sys").argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="zacos9-web-sharing-") as tmp:
    root = Path(tmp)
    home = root / "home"
    runtime = root / "runtime"
    pages = home / "Web Pages"
    pages.mkdir(parents=True)
    runtime.mkdir(mode=0o700)
    (pages / "index.html").write_text("hello from ZacOS\n", encoding="utf-8")
    secret = home / "secret.txt"
    secret.write_text("not public", encoding="utf-8")
    (pages / "outside-link").symlink_to(secret)

    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    env = os.environ | {
        "HOME": str(home),
        "XDG_RUNTIME_DIR": str(runtime),
        "ZACOS9_WEB_SHARING_PORT": str(port),
    }

    def run(command, expected=0):
        result = subprocess.run([sys.executable, str(script), command], env=env, text=True,
                                capture_output=True, timeout=10)
        assert result.returncode == expected, (command, result.stdout, result.stderr)
        return result

    assert run("status").stdout.strip() == "off"
    with socket.socket() as occupied:
        occupied.bind(("127.0.0.1", port))
        occupied.listen()
        failed = run("start", expected=1)
        assert f"could not start on port {port}" in failed.stderr
    assert run("status").stdout.strip() == "off"
    assert "is on at" in run("start").stdout
    try:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=0.5) as response:
                    assert response.read().decode() == "hello from ZacOS\n"
                break
            except (OSError, urllib.error.URLError):
                time.sleep(0.05)
        else:
            raise AssertionError("Web Sharing did not serve the Web Pages folder")

        assert run("status").stdout.strip() == "on"
        try:
            urllib.request.urlopen(f"http://127.0.0.1:{port}/outside-link", timeout=1)
            raise AssertionError("a symlink outside Web Pages was served")
        except urllib.error.HTTPError as exc:
            assert exc.code in (403, 404)
    finally:
        assert run("stop").stdout.strip() == "off"
    assert run("status").stdout.strip() == "off"

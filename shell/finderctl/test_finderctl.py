"""zacos9-finder-ctl: what it sends the Sniffer's socket (a stand-in here)."""
import os
import socket
import subprocess
import sys
import tempfile
import threading

ctl = sys.argv[1]
with tempfile.TemporaryDirectory() as runtime:
    env = dict(os.environ, XDG_RUNTIME_DIR=runtime, WAYLAND_DISPLAY="test-0",
               XDG_CONFIG_HOME=runtime)
    got = []

    def run(*args, code=0):
        r = subprocess.run([ctl, *args], env=env, cwd="/tmp", capture_output=True, text=True)
        assert r.returncode == code, (args, r.returncode, r.stderr)
        return r

    run("label", "2", "x", code=1)  # no Sniffer: says so
    server = socket.socket(socket.AF_UNIX)
    server.bind(f"{runtime}/zacos9-finder.test-0.sock")
    server.listen()

    def serve():
        while True:
            conn, _ = server.accept()
            with conn:
                data = b""
                while chunk := conn.recv(4096):
                    data += chunk
                got.extend(data.decode().splitlines())

    threading.Thread(target=serve, daemon=True).start()
    run("open", "/etc/hostname", "notes.txt")
    run("reveal", "/etc/hostname")
    run("label", "2", "/tmp/a b")
    run("label", "none", "/tmp/c")
    run("comment", "/tmp/a b", "From the CLI")
    run("comment", "/tmp/a b")
    run("command", "empty-trash")
    run("label", "purple-ish", "/tmp/a", code=2)
    run("comment", "/tmp/a\tb", "x", code=2)
    run("--help")
    import time
    time.sleep(0.3)
    assert got == [
        "cmd open-path /etc/hostname", "cmd open-path /tmp/notes.txt",
        "cmd reveal-path /etc/hostname",
        "cmd label-path 2\t/tmp/a b", "cmd label-path 0\t/tmp/c",
        "cmd comment-path /tmp/a b\tFrom the CLI", "cmd comment-path /tmp/a b\t",
        "cmd empty-trash",
    ], got
print("ok: zacos9-finder-ctl sends the Sniffer its commands")

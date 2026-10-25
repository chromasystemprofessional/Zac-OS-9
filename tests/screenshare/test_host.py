#!/usr/bin/env python3
"""zacos9-screenshare-host against a stand-in wayvnc: the private socket,
the bytes passed through, Activity Monitor's list and Disconnect, the
settings' reload, one host at a time, and quitting. Asking is off (the
alert needs a person); Bonjour is off."""
import os
from pathlib import Path
import socket
import stat
import subprocess
import sys
import tempfile
import time

host = Path(sys.argv[1]).resolve()

FAKE_WAYVNC = r'''#!/usr/bin/env python3
import os, socket, sys, threading
with open(os.environ["TEST_WAYVNC_LOG"], "a") as log:
    log.write(" ".join(sys.argv[1:]) + "\n")
path = sys.argv[-1]
server = socket.socket(socket.AF_UNIX)
server.bind(path)
server.listen()
def serve(c):
    c.sendall(b"RFB 003.008\n")
    while data := c.recv(65536):
        c.sendall(data)
    c.close()
while True:
    c, _ = server.accept()
    threading.Thread(target=serve, args=(c,), daemon=True).start()
'''


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def request(sock_path, line):
    with socket.socket(socket.AF_UNIX) as s:
        s.settimeout(10)
        s.connect(str(sock_path))
        s.sendall(line.encode() + b"\n")
        out = b""
        while data := s.recv(65536):
            out += data
    return out.decode().split("\n")


def wait(cond, what, timeout=15):
    end = time.time() + timeout
    while time.time() < end:
        if cond():
            return
        time.sleep(0.1)
    raise AssertionError(f"timed out waiting for {what}")


with tempfile.TemporaryDirectory(prefix="zacos9-screenshare-") as temp:
    root = Path(temp)
    runtime = root / "runtime"
    runtime.mkdir(mode=0o700)
    config = root / "config"
    (config / "zacos9").mkdir(parents=True)
    (config / "zacos9" / "screensharing.conf").write_text("ask=no\ncontrol=yes\n")
    sharing = root / "sharing"
    sharing.mkdir()
    (sharing / "sharing.conf").write_text("name=Test Computer\n")
    fake = root / "wayvnc"
    fake.write_text(FAKE_WAYVNC)
    fake.chmod(0o700)
    log = root / "wayvnc.log"
    port = free_port()
    env = os.environ | {
        "HOME": str(root),
        "XDG_RUNTIME_DIR": str(runtime),
        "XDG_CONFIG_HOME": str(config),
        "XDG_STATE_HOME": str(root / "state"),
        "WAYLAND_DISPLAY": "wayland-test-none",
        "QT_QPA_PLATFORM": "offscreen",
        "ZACOS9_SHARING_CONF": str(sharing),
        "ZACOS9_WAYVNC": str(fake),
        "ZACOS9_AVAHI_PUBLISH": "none",
        "ZACOS9_SCREENSHARE_PORT": str(port),
        "TEST_WAYVNC_LOG": str(log),
    }
    control = runtime / "zacos9-screenshare" / "host.sock"
    proc = subprocess.Popen([str(host)], env=env)
    try:
        wait(control.exists, "host.sock")
        assert request(control, "status")[0] == "on"

        # wayvnc: TLS and PAM, on our socket, never on the network.
        share = runtime / "zacos9-screenshare"
        assert stat.S_IMODE(share.stat().st_mode) == 0o700
        conf = (share / "wayvnc.conf").read_text()
        assert "enable_auth=true" in conf and "enable_pam=true" in conf, conf
        args = log.read_text().split()
        assert "-u" in args and "-d" not in args and args[-1] == str(share / "vnc.sock"), args
        key = config / "zacos9" / "screensharing" / "key.pem"
        assert stat.S_IMODE(key.stat().st_mode) == 0o600
        assert (config / "zacos9" / "screensharing" / "cert.pem").exists()

        # Through: the server speaks first, then everything is echoed.
        viewer = socket.create_connection(("127.0.0.1", port), timeout=10)
        assert viewer.recv(12) == b"RFB 003.008\n"
        payload = os.urandom(3 << 20)
        viewer.setblocking(True)
        got = bytearray()
        sent = 0
        viewer.settimeout(10)
        while len(got) < len(payload):
            if sent < len(payload):
                sent += viewer.send(payload[sent:sent + 65536])
            try:
                viewer.settimeout(0.01 if sent < len(payload) else 10)
                got += viewer.recv(1 << 20)
            except socket.timeout:
                pass
        assert bytes(got) == payload

        # Activity Monitor: who, and Disconnect.
        conns = [l.split("\t") for l in request(control, "status") if l.startswith("conn\t")]
        assert len(conns) == 1 and conns[0][2] == "127.0.0.1", conns
        assert request(control, f"disconnect {conns[0][1]}")[0] == "ok"
        viewer.settimeout(10)
        assert viewer.recv(1) == b""
        viewer.close()
        wait(lambda: not [l for l in request(control, "status") if l.startswith("conn\t")],
             "the connection to go")
        assert request(control, "disconnect 99")[0] != "ok"

        # No mouse and keyboard: wayvnc again, with -d.
        (config / "zacos9" / "screensharing.conf").write_text("ask=no\ncontrol=no\n")
        assert request(control, "reload")[0] == "ok"
        assert "-d" in log.read_text().splitlines()[-1].split()
        viewer = socket.create_connection(("127.0.0.1", port), timeout=10)
        assert viewer.recv(12) == b"RFB 003.008\n"
        viewer.close()

        # One host at a time: a second one leaves the first be.
        second = subprocess.run([str(host)], env=env, timeout=10)
        assert second.returncode == 0
        assert request(control, "status")[0] == "on"

        assert request(control, "quit")[0] == "ok"
        assert proc.wait(timeout=10) == 0
        wait(lambda: subprocess.run(["pgrep", "-f", str(fake)], capture_output=True).returncode != 0,
             "wayvnc to stop")
        try:
            socket.create_connection(("127.0.0.1", port), timeout=2).close()
            raise AssertionError("port still open after quit")
        except ConnectionRefusedError:
            pass
    finally:
        if proc.poll() is None:
            proc.kill()
        subprocess.run(["pkill", "-f", str(fake)])
print("ok")

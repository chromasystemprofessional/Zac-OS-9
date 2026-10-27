#!/usr/bin/env python3
"""zacos9-screenshare-host against a stand-in wayvnc: the private socket,
the bytes passed through, the sound (a stand-in parec; the test build
takes "sesame" for the password; heard by the viewer's SoundClient,
test-screenshare-sound, with a stand-in pacat), Activity Monitor's list and
Disconnect, the settings' reload, one host at a time, and quitting.
Asking is off (the alert needs a person); Bonjour is off."""
import getpass
import hashlib
import os
from pathlib import Path
import socket
import ssl
import stat
import subprocess
import sys
import tempfile
import time

from zacos9test import free_port

host = Path(sys.argv[1]).resolve()
sound_client = Path(sys.argv[2]).resolve()

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

# What this computer "plays": bytes 0, 1, 2 ... 255, 0, 1 ... on and on.
FAKE_PAREC = r'''#!/usr/bin/env python3
import os, sys, time
with open(os.environ["TEST_PAREC_LOG"], "a") as log:
    log.write(" ".join(sys.argv[1:]) + "\n")
chunk = bytes(range(256)) * 16
while True:
    sys.stdout.buffer.write(chunk)
    sys.stdout.buffer.flush()
    time.sleep(0.01)
'''

# What the viewer "plays": what comes, kept.
FAKE_PACAT = r'''#!/usr/bin/env python3
import os, sys
with open(os.environ["TEST_PACAT_LOG"], "a") as log:
    log.write(" ".join(sys.argv[1:]) + "\n")
with open(os.environ["TEST_PACAT_OUT"], "ab", buffering=0) as out:
    while data := sys.stdin.buffer.read1(65536):
        out.write(data)
'''


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


def sound(port, user, password):
    """The sound's login: (its certificate, its answer, the socket)."""
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    tls.check_hostname = False
    tls.verify_mode = ssl.CERT_NONE
    raw = socket.create_connection(("127.0.0.1", port), timeout=10)
    s = tls.wrap_socket(raw)
    s.sendall(f"ZACOS-SOUND 1\n{user}\n{password}\n".encode())
    line = b""
    while not line.endswith(b"\n"):
        data = s.recv(1)
        if not data:
            break
        line += data
    return s.getpeercert(binary_form=True), line.decode().strip(), s


def closed(s):
    """True once the other end has hung up (whatever was still coming)."""
    s.settimeout(10)
    try:
        while s.recv(65536):
            pass
    except (ssl.SSLError, ConnectionResetError):
        pass
    return True


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
    parec = root / "parec"
    parec.write_text(FAKE_PAREC)
    parec.chmod(0o700)
    parec_log = root / "parec.log"
    pacat = root / "pacat"
    pacat.write_text(FAKE_PACAT)
    pacat.chmod(0o700)
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
        "ZACOS9_PAREC": str(parec),
        "TEST_PAREC_LOG": str(parec_log),
        "ZACOS9_AVAHI_PUBLISH": "none",
        "ZACOS9_SCREENSHARE_PORT": str(port),
        "TEST_WAYVNC_LOG": str(log),
    }
    control = runtime / "zacos9-screenshare" / "host.sock"
    proc = subprocess.Popen([str(host)], env=env)
    user = getpass.getuser()
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

        # The sound: its own port, the same certificate, the owner's password.
        cert = ssl.PEM_cert_to_DER_cert((config / "zacos9" / "screensharing" / "cert.pem").read_text())
        der, answer, s = sound(port + 1, user, "wrong")
        assert der == cert
        assert answer.startswith("The name or password"), answer
        assert closed(s)
        _, answer, s = sound(port + 1, "someone-else", "sesame")
        assert answer.startswith("The name or password"), answer
        s.close()
        _, answer, listening = sound(port + 1, user, "sesame")
        assert answer == "ok 48000 2", answer
        heard = b""
        while len(heard) < 8192:
            heard += listening.recv(65536)
        # Whole frames, in order, from where parec began.
        start = heard[0]
        assert heard[:512] == bytes((start + i) % 256 for i in range(512)), heard[:16]
        args = parec_log.read_text().split()
        assert "--raw" in args and "--format=s16le" in args and "--device=@DEFAULT_MONITOR@" in args, args

        # The viewer's side: the certificate checked, then played.
        digest = hashlib.sha256(cert).hexdigest().upper()
        fingerprint = " ".join(digest[i:i + 4] for i in range(0, len(digest), 4))
        client_env = env | {"ZACOS9_PACAT": str(pacat), "TEST_PACAT_LOG": str(root / "pacat.log"),
                            "TEST_PACAT_OUT": str(root / "played")}
        out = subprocess.run([str(sound_client), "127.0.0.1", str(port + 1), fingerprint, user, "sesame", "2"],
                             env=client_env, capture_output=True, text=True, timeout=20).stdout
        assert out.startswith("playing"), out
        played = (root / "played").read_bytes()
        assert len(played) > 4096, len(played)
        assert played[:512] == bytes((played[0] + i) % 256 for i in range(512))
        pargs = (root / "pacat.log").read_text().split()
        assert "--playback" in pargs and "--rate=48000" in pargs and "--channels=2" in pargs, pargs
        # Another certificate than the screen's: not played.
        out = subprocess.run([str(sound_client), "127.0.0.1", str(port + 1), "AB12 " * 15 + "AB12", user,
                              "sesame", "2"], env=client_env, capture_output=True, text=True, timeout=20).stdout
        assert out.startswith("stopped: The sound came with a different certificate"), out
        out = subprocess.run([str(sound_client), "127.0.0.1", str(port + 1), fingerprint, user, "nope", "5"],
                             env=client_env, capture_output=True, text=True, timeout=20).stdout
        assert out.startswith("stopped: The name or password"), out

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
        # Its sound went with it, and parec stopped.
        assert closed(listening)
        listening.close()
        wait(lambda: subprocess.run(["pgrep", "-f", str(parec)], capture_output=True).returncode != 0,
             "parec to stop")
        # No sound for a computer that isn't viewing the screen.
        _, answer, s = sound(port + 1, user, "sesame")
        assert "isn’t viewing" in answer, answer
        s.close()

        # No mouse and keyboard: wayvnc again, with -d.
        (config / "zacos9" / "screensharing.conf").write_text("ask=no\ncontrol=no\nsound=no\n")
        assert request(control, "reload")[0] == "ok"
        # No sound shared: its port closed.
        try:
            socket.create_connection(("127.0.0.1", port + 1), timeout=2).close()
            raise AssertionError("sound port still open with sound=no")
        except ConnectionRefusedError:
            pass
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
        subprocess.run(["pkill", "-f", str(parec)])
print("ok")

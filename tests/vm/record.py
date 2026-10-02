"""Record a VM's screen through QEMU's monitor: a screenshot every STEP
seconds for SECONDS, as OUT/frame-NNNN.png, with their times in
OUT/times.txt.

usage: record.py MONITOR_SOCKET OUT SECONDS STEP
"""
import os
import socket
import sys
import time

sock_path, out, seconds, step = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
s = socket.socket(socket.AF_UNIX)
s.connect(sock_path)
s.settimeout(2)


def command(c):
    s.sendall(c.encode() + b"\n")
    buf = b""
    while not buf.endswith(b"(qemu) "):
        try:
            chunk = s.recv(65536)
        except socket.timeout:
            break
        if not chunk:
            break
        buf += chunk


command("")  # the banner
start = time.time()
n = 0
with open(os.path.join(out, "times.txt"), "w") as times:
    while time.time() - start < seconds:
        t = time.time() - start
        command(f"screendump {out}/frame-{n:04d}.png -f png")
        times.write(f"{n} {t:.2f}\n")
        n += 1
        time.sleep(max(0, step - (time.time() - start - t)))

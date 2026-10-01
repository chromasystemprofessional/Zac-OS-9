"""Send commands to a QEMU monitor socket (scripts/vm.sh) and print replies.

usage: monitor.py SOCKET COMMAND...
"""
import socket
import sys
import time

s = socket.socket(socket.AF_UNIX)
s.connect(sys.argv[1])
s.settimeout(2)


def drain():
    out = b""
    try:
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            out += chunk
            if out.endswith(b"(qemu) "):
                break
    except socket.timeout:
        pass
    return out.decode(errors="replace")


drain()  # banner and first prompt
for command in sys.argv[2:]:
    s.sendall(command.encode() + b"\n")
    reply = drain().replace("(qemu) ", "").splitlines()
    # The monitor echoes the command line first (with terminal escapes).
    print("\n".join(line for line in reply[1:] if line.strip()))
    time.sleep(0.2)

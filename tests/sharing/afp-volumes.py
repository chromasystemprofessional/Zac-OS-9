"""Log in to an AFP server the way Mac OS 9 does over TCP/IP (AFP 2.2) and
list the volumes it offers, as the Chooser does before you pick one.

usage: afp-volumes.py HOST [PORT] [USER PASSWORD]

Without a user it logs in as a guest ("No User Authent"); with one it
uses clear-text passwords ("Cleartxt Passwrd"), which the server only
offers if File Sharing allows them.
"""
import socket
import struct
import sys

DSI_CLOSE, DSI_COMMAND, DSI_OPEN = 1, 2, 4
FP_GET_SRVR_PARMS, FP_LOGIN, FP_LOGOUT = 16, 18, 20

args = sys.argv[1:]
host = args.pop(0)
port = int(args.pop(0)) if args and args[0].isdigit() else 548
user, password = (args + [None, None])[:2]

s = socket.create_connection((host, port), timeout=5)
next_id = 0


def recv(n):
    data = b""
    while len(data) < n:
        chunk = s.recv(n - len(data))
        if not chunk:
            sys.exit("the server hung up")
        data += chunk
    return data


def dsi(command, payload=b""):
    """One DSI request and its reply: (AFP result code, data)."""
    global next_id
    next_id += 1
    s.sendall(struct.pack(">BBHIII", 0, command, next_id, 0, len(payload), 0) + payload)
    _, _, rid, err, length, _ = struct.unpack(">BBHIII", recv(16))
    data = recv(length)
    return struct.unpack(">i", struct.pack(">I", err))[0], data


def pstr(text):
    raw = text.encode("mac_roman")
    return bytes([len(raw)]) + raw


dsi(DSI_OPEN, bytes([1, 4]) + struct.pack(">I", 1024))  # attention quantum
login = bytes([FP_LOGIN]) + pstr("AFP2.2")
if user is None:
    login += pstr("No User Authent")
else:
    login += pstr("Cleartxt Passwrd") + pstr(user)
    if len(login) % 2:
        login += b"\0"
    login += password.encode("mac_roman")[:8].ljust(8, b"\0")
err, _ = dsi(DSI_COMMAND, login)
if err:
    sys.exit("login refused (AFP error %d)" % err)
print("logged in as", user or "a guest")

err, data = dsi(DSI_COMMAND, bytes([FP_GET_SRVR_PARMS, 0]))
if err:
    sys.exit("FPGetSrvrParms failed (AFP error %d)" % err)
count, off = data[4], 5
for _ in range(count):
    flags, n = data[off], data[off + 1]
    name = data[off + 2:off + 2 + n].decode("mac_roman")
    print("volume:", name, "(has a password)" if flags & 0x80 else "")
    off += 2 + n
dsi(DSI_COMMAND, bytes([FP_LOGOUT, 0]))
s.sendall(struct.pack(">BBHIII", 0, DSI_CLOSE, next_id + 1, 0, 0, 0))
s.close()

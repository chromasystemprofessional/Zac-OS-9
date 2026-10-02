"""Ask an AFP server about itself (DSIGetStatus / FPGetSrvrInfo), as a Mac's
Chooser does before logging in, and print its name, AFP versions and
login methods (UAMs).

usage: afp-info.py HOST [PORT]
"""
import socket
import struct
import sys

host = sys.argv[1]
port = int(sys.argv[2]) if len(sys.argv) > 2 else 548

s = socket.create_connection((host, port), timeout=5)
# DSI header: flags 0 (request), command 3 (GetStatus), request id 1,
# error/offset 0, data length 0, reserved 0.
s.sendall(struct.pack(">BBHIII", 0, 3, 1, 0, 0, 0))
head = b""
while len(head) < 16:
    head += s.recv(16 - len(head))
flags, cmd, rid, err, length, _ = struct.unpack(">BBHIII", head)
data = b""
while len(data) < length:
    data += s.recv(length - len(data))
s.close()


def pstring(off):
    n = data[off]
    return data[off + 1:off + 1 + n].decode("mac_roman")


def plist(off):
    count = data[off]
    items, off = [], off + 1
    for _ in range(count):
        items.append(pstring(off))
        off += 1 + data[off]
    return items


# FPGetSrvrInfo reply: offsets to machine type, AFP versions, UAMs, icon;
# flags; then the server name as a Pascal string.
machine_off, versions_off, uams_off, icon_off, srv_flags = struct.unpack(">HHHHH", data[:10])
print("name:", pstring(10))
print("machine:", pstring(machine_off))
print("afp versions:", ", ".join(plist(versions_off)))
print("uams:", ", ".join(plist(uams_off)))
print("flags: 0x%04x" % srv_flags)

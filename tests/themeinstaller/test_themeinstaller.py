import importlib.util
import os
import struct
import subprocess
import sys
import tempfile

binary = sys.argv[1]

# A minimal HFS resource fork: 16-byte header, 256-byte pad, resource data
# (each prefixed by a 4-byte length), then the resource map.
def build_fork(resources):
    data = b""
    for rtype, body in resources:
        data += struct.pack(">I", len(body)) + body
    from collections import OrderedDict
    counts = OrderedDict()
    for rtype, body in resources:
        counts.setdefault(rtype, 0)
        counts[rtype] += 1
    type_list = struct.pack(">H", len(counts) - 1)
    header_size = 2 + 8 * len(counts)
    cursor = header_size
    for rtype, n in counts.items():
        type_list += struct.pack(">4sH H", rtype, n - 1, cursor)
        cursor += 2 + 12 * n
    for rtype, n in counts.items():
        type_list += struct.pack(">H", n - 1)
        for _, body in resources:
            if _ != rtype:
                continue
            type_list += struct.pack(">H", 0)
            type_list += struct.pack(">H", 0xFFFF)
            type_list += bytes([0])
            type_list += struct.pack(">I", 0)[1:]
            type_list += struct.pack(">I", 0)
    map_offset = 256 + len(data)
    map_size = 28 + len(type_list)
    header = struct.pack(">IIII", 256, map_offset, len(data), map_size)
    return header + b"\x00" * 240 + data + (header + struct.pack(">I H H", 0, 0, 0)
        + struct.pack(">HH", 28, 28 + len(type_list)) + type_list)


def make_theme_file(directory):
    icl8 = b"\x00" * 1024
    icn = b"\xff" * 128 + b"\xff" * 128
    clut = struct.pack(">I", 0) + struct.pack(">HH", 0, 0)
    for i in range(1):
        clut += struct.pack(">HHHH", i, i * 255, i * 255, i * 255)
    fork = build_fork([(b"icl8", icl8), (b"ICN#", icn), (b"clut", clut)])
    path = os.path.join(directory, "Apple platinum copy")
    with open(path, "wb") as f:
        f.write(fork)
    return path


def run(binary, file, name, env):
    return subprocess.run([binary, "--file", file, "--name", name],
                          env=env, capture_output=True, text=True, timeout=60)


# 1. A file with no Mac OS 9 artwork: the installer says so.
empty = tempfile.mkdtemp()
with open(os.path.join(empty, "a.png"), "wb") as f:
    f.write(b"png")
env = dict(os.environ)
env["XDG_CONFIG_HOME"] = tempfile.mkdtemp()
env["XDG_DATA_HOME"] = tempfile.mkdtemp()
env["HOME"] = tempfile.mkdtemp()
env["QT_QPA_PLATFORM"] = "offscreen"
r = run(binary, os.path.join(empty, "a.png"), "Test", env)
assert r.returncode != 0, "an artless file is refused"
assert "no Mac OS 9 artwork" in r.stderr, "the message names the cause"
print("ok    artless file is refused with instructions")

# 2. A theme file's resource fork: the installer succeeds and names it.
src = tempfile.mkdtemp()
theme = make_theme_file(src)
env2 = dict(os.environ)
config = tempfile.mkdtemp()
data = tempfile.mkdtemp()
env2["XDG_CONFIG_HOME"] = config
env2["XDG_DATA_HOME"] = data
env2["HOME"] = tempfile.mkdtemp()
env2["QT_QPA_PLATFORM"] = "offscreen"
# No --name: the name is inherited from the file ("Apple platinum").
r = subprocess.run([binary, "--file", theme], env=env2,
                   capture_output=True, text=True, timeout=60)
assert r.returncode == 0, r.stderr
out = os.path.join(config, "zacos9", "themes", "Apple platinum")
assert os.path.isfile(os.path.join(out, "menu-icon.png")), "menu icon written"
assert os.path.isfile(os.path.join(out, "theme.conf")), "theme.conf written"
print("ok    theme file installs and inherits its name")

print("OK")

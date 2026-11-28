#!/usr/bin/env python3
"""The HFS reader on truncated and corrupted images.

Every malformed image must end in a clean error (exit 2) or a valid listing,
never a traceback or a hang.

usage: test_mac_hfs_malformed.py TOOL
"""
import os
import random
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hfs_fixture import make_hfs_image

tool = sys.argv[1]
fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    fails += 0 if ok else 1


def run(image, args, timeout=5):
    with tempfile.NamedTemporaryFile(suffix=".dsk") as image_file:
        image_file.write(image)
        image_file.flush()
        return subprocess.run([sys.executable, tool, "--image", image_file.name,
                               *args], capture_output=True, text=True,
                              timeout=timeout)


def refuses(name, image, args=("--list",), timeout=5):
    try:
        result = run(image, args, timeout)
    except subprocess.TimeoutExpired:
        check(False, name + ": timed out")
        return
    output = result.stdout + result.stderr
    last = result.stderr.strip().splitlines()[-1] if result.stderr.strip() else ""
    check(result.returncode == 2 and "Traceback" not in output,
          f"{name}: rejected cleanly ({last[:70]})")


base = make_hfs_image()
result = run(base, ["--list"])
check(result.returncode == 0 and "System Folder" in result.stdout,
      "the fixture is listed")

for size in (0, 1100, 1500, 2048, 2500):
    refuses(f"truncated to {size}", base[:size])

# A B-tree record offset outside the node used to index past its bytes.
node = 2048
bad = bytearray(base)
bad[node + 510:node + 512] = struct.pack(">H", 0xFFF0)
refuses("record offset outside the node", bad)

# The first folder record's own ID becomes the root's: copying it would
# recurse into the root for ever.
loop = bytearray(base)
key_offset = struct.unpack(">H", base[node + 510:node + 512])[0]
key_length = base[node + key_offset]
record = key_offset + 2 + key_length
if record & 1:
    record += 1
loop[node + record + 4:node + record + 8] = struct.pack(">I", 2)
with tempfile.TemporaryDirectory() as directory:
    destination = os.path.join(directory, "out")
    try:
        result = run(loop, ["--destination", destination])
    except subprocess.TimeoutExpired:
        check(False, "folder loop: timed out")
    else:
        output = result.stdout + result.stderr
        check(result.returncode == 2 and "folder loop" in result.stderr and
              "Traceback" not in output, "folder loop: rejected cleanly")

# Deterministic bit flips over the whole image: whichever field they reach,
# the reader must answer or refuse, not crash.
rng = random.Random(20261128)
for index in range(25):
    mutated = bytearray(base)
    for _ in range(rng.randint(1, 6)):
        position = rng.randrange(len(mutated))
        mutated[position] ^= 1 << rng.randrange(8)
    try:
        result = run(bytes(mutated), ["--list"], timeout=3)
    except subprocess.TimeoutExpired:
        check(False, f"bit flip {index}: timed out")
        continue
    output = result.stdout + result.stderr
    check(result.returncode in (0, 2) and "Traceback" not in output,
          f"bit flip {index}: exit {result.returncode}")

print(f"{fails} failure{'s' if fails != 1 else ''}")
sys.exit(1 if fails else 0)

#!/usr/bin/env python3
"""zacos9-mac-mount's APFS reader: helper-volume filtering and bounded parsing.

usage: test_mac_mount.py HELPER
"""
import ctypes.util
import importlib.machinery
import importlib.util
import os
import random
import signal
import sys
import tempfile
import time
from pathlib import Path

helper = Path(sys.argv[1])
loader = importlib.machinery.SourceFileLoader("zacos9_mac_mount", str(helper))
spec = importlib.util.spec_from_loader(loader.name, loader)
module = importlib.util.module_from_spec(spec)
loader.exec_module(module)

fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    fails += 0 if ok else 1


# The helper's own APFS volumes (Preboot, Recovery, VM, ...) are not mounted.
volumes = [
    {"index": 1, "name": "Macintosh HD"},
    {"index": 2, "name": "Preboot"},
    {"index": 3, "name": "Recovery"},
    {"index": 4, "name": "Data"},
]
check([v["name"] for v in module.user_apfs_volumes(volumes)] == ["Macintosh HD", "Data"],
      "system helper volumes are left out")
try:
    module.user_apfs_volumes([{"index": 1, "name": "Preboot"}])
except ValueError:
    check(True, "a container with only helper volumes is refused")
else:
    check(False, "a container with only helper volumes is refused")

# The reader runs in a child: a result comes back, a hang is killed, and a
# crash does not take the helper down.
original = module.apfs_volumes
original_timeout = module.APFS_TIMEOUT
module.apfs_volumes = lambda source: [{"index": 1, "name": "Macintosh HD"}]
check(module.apfs_volumes_bounded("unused") == [{"index": 1, "name": "Macintosh HD"}],
      "the child sends the volume list back")

module.APFS_TIMEOUT = 1
module.apfs_volumes = lambda source: time.sleep(60)
start = time.monotonic()
try:
    module.apfs_volumes_bounded("unused")
except ValueError as error:
    check("did not finish" in str(error) and time.monotonic() - start < 5,
          "a hung reader is killed")
else:
    check(False, "a hung reader is killed")

module.apfs_volumes = lambda source: os.kill(os.getpid(), signal.SIGSEGV)
try:
    module.apfs_volumes_bounded("unused")
except ValueError as error:
    check("crashed" in str(error), "a crashing reader is contained")
else:
    check(False, "a crashing reader is contained")

module.apfs_volumes = original
module.APFS_TIMEOUT = original_timeout

# Real libfsapfs on malformed images: the root helper must survive whatever the
# library does. A crash or a hang is contained and reported as a value error.
if ctypes.util.find_library("fsapfs") is None:
    print("note: libfsapfs isn't installed; skipped the malformed images")
else:
    module.APFS_TIMEOUT = 5
    rng = random.Random(1234)
    with tempfile.TemporaryDirectory() as directory:
        images = {
            "empty": b"",
            "ones": b"\xff" * 4096,
            "random": bytes(rng.randrange(256) for _ in range(16384)),
        }
        apfs = bytearray(4096)
        apfs[32:36] = b"NXSB"
        apfs[36:40] = (4096).to_bytes(4, "little")
        images["truncated-apfs"] = bytes(apfs)
        for name, data in images.items():
            path = Path(directory) / name
            path.write_bytes(data)
            try:
                module.apfs_volumes_bounded(path)
            except ValueError as error:
                check("reader" in str(error) or "APFS" in str(error),
                      f"malformed image {name}: {str(error).splitlines()[0][:60]}")
            else:
                check(False, f"malformed image {name} is refused")
    module.APFS_TIMEOUT = original_timeout

print(f"{fails} failure{'s' if fails != 1 else ''}")
sys.exit(1 if fails else 0)

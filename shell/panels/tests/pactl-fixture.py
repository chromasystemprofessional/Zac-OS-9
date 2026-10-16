#!/usr/bin/python3
import fcntl
import json
import os
import sys
import time
from pathlib import Path

path = Path(os.environ["SOUND_TEST_STATE"])
args = sys.argv[1:]
with path.with_suffix(".log").open("a") as log:
    log.write(json.dumps(args) + "\n")
if os.environ.get("SOUND_TEST_FAIL") == args[0]:
    print("simulated sound server failure", file=sys.stderr)
    sys.exit(1)
if os.environ.get("SOUND_TEST_TIMEOUT"):
    time.sleep(10)

lock = path.with_suffix(".lock").open("a")
fcntl.flock(lock, fcntl.LOCK_EX)
state = json.loads(path.read_text())
if args == ["get-default-sink"]:
    print(state["default"])
elif args == ["get-default-source"]:
    print(state["default_source"])
elif args == ["--format=json", "list", "sinks"]:
    print(json.dumps(state["sinks"]))
elif args == ["--format=json", "list", "sources"]:
    print(json.dumps(state["sources"]))
elif args == ["--format=json", "list", "sink-inputs"]:
    print(json.dumps(state["inputs"]))
elif args[0] == "set-default-sink":
    state["default"] = args[1]
elif args[0] == "set-default-source":
    state["default_source"] = args[1]
elif args[0] == "set-sink-port":
    sink = next(s for s in state["sinks"] if s["name"] == args[1])
    sink["active_port"] = args[2]
elif args[0] == "move-sink-input":
    stream = next(s for s in state["inputs"] if str(s["index"]) == args[1])
    stream["target"] = args[2]
elif args[0] == "set-sink-volume":
    sink = next(s for s in state["sinks"] if s["name"] == args[1])
    channels = list(sink["volume"].values())
    levels = args[2:]
    if len(levels) == 1:
        levels *= len(channels)
    for channel, level in zip(channels, levels):
        channel["value"] = int(level.rstrip("%")) * 65536 / 100
elif args[0] == "set-sink-mute":
    sink = next(s for s in state["sinks"] if s["name"] == args[1])
    sink["mute"] = args[2] == "1"
else:
    print("unexpected pactl arguments", file=sys.stderr)
    sys.exit(1)
if args[0].startswith("set-") or args[0] == "move-sink-input":
    temporary = path.with_suffix(f".{os.getpid()}.tmp")
    temporary.write_text(json.dumps(state))
    temporary.replace(path)

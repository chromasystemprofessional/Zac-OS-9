#!/usr/bin/python3
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

state = json.loads(path.read_text())
if args == ["get-default-sink"]:
    print(state["default"])
elif args == ["--format=json", "list", "sinks"]:
    print(json.dumps(state["sinks"]))
elif args == ["--format=json", "list", "sink-inputs"]:
    print(json.dumps(state["inputs"]))
elif args[0] == "set-default-sink":
    state["default"] = args[1]
elif args[0] == "set-sink-port":
    sink = next(s for s in state["sinks"] if s["name"] == args[1])
    sink["active_port"] = args[2]
elif args[0] == "move-sink-input":
    stream = next(s for s in state["inputs"] if str(s["index"]) == args[1])
    stream["target"] = args[2]
elif args[0] == "set-sink-volume":
    sink = next(s for s in state["sinks"] if s["name"] == args[1])
    for channel in sink["volume"].values():
        channel["value"] = int(args[2].rstrip("%")) * 65536 / 100
elif args[0] == "set-sink-mute":
    sink = next(s for s in state["sinks"] if s["name"] == args[1])
    sink["mute"] = args[2] == "1"
else:
    print("unexpected pactl arguments", file=sys.stderr)
    sys.exit(1)
path.write_text(json.dumps(state))

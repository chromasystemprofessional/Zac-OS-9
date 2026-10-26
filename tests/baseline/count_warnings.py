#!/usr/bin/env python3
"""Compiler warnings in a build log, each counted once (a file compiled for
several programs warns once per compile), against tests/baseline/warnings:
more fails, and they're listed. Fewer passes, with a reminder to lower the
number so it stays low.

    count_warnings.py BUILD.LOG [BASELINE]
"""
import re
import sys
from pathlib import Path

log = Path(sys.argv[1]).read_text(errors="replace")
warning = re.compile(r"^(?:\S*?/)?(?:\.\./)*((?:[\w.+-]+/)*[\w.+-]+):(\d+):(\d+): warning: (.*)$", re.M)
found = sorted({f"{m[1]}:{m[2]}:{m[3]}: {m[4]}" for m in warning.finditer(log)})
print(f"{len(found)} distinct compiler warnings")
if len(sys.argv) < 3:
    sys.exit(0)
baseline_file = Path(sys.argv[2])
allowed = int(baseline_file.read_text().split()[0])
if len(found) > allowed:
    print(f"\nMore than the {allowed} in {baseline_file}. All of them:")
    print("\n".join(found))
    sys.exit(1)
if len(found) < allowed:
    print(f"Fewer than the {allowed} in {baseline_file}: lower it to {len(found)}.")

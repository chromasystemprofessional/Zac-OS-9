#!/usr/bin/env python3
"""ShellCheck the privileged helpers under system/.

usage: test_shellcheck.py SOURCE_ROOT

Only files whose shebang is sh, bash, dash or ksh are checked; the helpers
that are Python (the disk, APFS and module helpers) are out of scope. Skips
with exit 77 when shellcheck is not installed, so a source build without the
tool still runs the rest of the suite.
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
shellcheck = shutil.which("shellcheck")
if not shellcheck:
    print("skip: shellcheck isn't installed (apt install shellcheck)")
    sys.exit(77)

shell_re = re.compile(
    r"^#!\s*(?:/usr/bin/env\s+)?(?:/usr/bin/|/bin/)?(?:sh|bash|dash|ksh)(?:\s|$)")
helpers = []
for path in sorted((root / "system").rglob("*")):
    if not path.is_file():
        continue
    try:
        first = path.open(encoding="utf-8", errors="replace").readline()
    except OSError:
        continue
    if shell_re.match(first):
        helpers.append(str(path.relative_to(root)))

if not helpers:
    print("system-shellcheck: no shell helpers found", file=sys.stderr)
    sys.exit(1)

result = subprocess.run(
    [shellcheck, "-x", "-S", "warning", "-f", "gcc", *helpers],
    cwd=root, capture_output=True, text=True)
if result.stdout:
    print(result.stdout, end="")
if result.stderr:
    print(result.stderr, end="", file=sys.stderr)
if result.returncode != 0:
    sys.exit(1)

print(f"system-shellcheck: {len(helpers)} helpers clean")

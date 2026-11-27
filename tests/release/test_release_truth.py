#!/usr/bin/env python3
"""One version across the release: the changelog, Meson, PROJECT_STATUS, the
README's "New in" section and the What's New notes.

    test_release_truth.py VERSION SOURCE_ROOT
"""
import re
import subprocess
import sys
from pathlib import Path

# The first release whose notes are required. Older releases predate the
# What's New window or were shipped before this check existed.
NOTES_SINCE = "1.18.19"

version, root = sys.argv[1], Path(sys.argv[2])
errors = []


def fail(message):
    errors.append(message)


def at_least(candidate, baseline):
    """Debian version order, so 1.18.19.1 counts as newer than 1.18.19."""
    try:
        return subprocess.run(
            ["dpkg", "--compare-versions", candidate, "ge", baseline],
            capture_output=True).returncode == 0
    except FileNotFoundError:
        def key(value):
            return tuple(int(part) for part in re.findall(r"\d+", value))
        return key(candidate) >= key(baseline)


changelog = (root / "debian/changelog").read_text()
versions = re.findall(r"^zacos9 \(([^)]+)\)", changelog, re.MULTILINE)
if not versions:
    raise SystemExit("release-truth: no versions in debian/changelog")
latest = versions[0]

if version != latest:
    fail(f"Meson says {version}, but debian/changelog starts at {latest}")

status = (root / "PROJECT_STATUS.md").read_text()
match = re.search(r"Latest release:\s*\*\*([^*]+)\*\*", status)
if not match:
    fail("PROJECT_STATUS.md has no 'Latest release: **...**' line")
elif match.group(1) != latest:
    fail(f"PROJECT_STATUS.md says {match.group(1)}, but the changelog says {latest}")

major_minor = ".".join(latest.split(".")[:2])
readme = (root / "README.md").read_text()
if not re.search(rf"^## New in {re.escape(major_minor)}\b", readme, re.MULTILINE):
    fail(f"README.md has no '## New in {major_minor}' section for {latest}")

notes = root / "shell/update/whatsnew"
update_meson = (root / "shell/update/meson.build").read_text()
checked = 0
for release in versions:
    if not at_least(release, NOTES_SINCE):
        continue
    checked += 1
    note = notes / f"{release}.txt"
    if not note.is_file():
        fail(f"missing What's New note: {note.relative_to(root)}")
        continue
    if f"'whatsnew/{release}.txt'" not in update_meson:
        fail(f"shell/update/meson.build does not install whatsnew/{release}.txt")
    if not note.read_text().strip():
        fail(f"{note.relative_to(root)} is empty")

if errors:
    for error in errors:
        print(f"release-truth: {error}", file=sys.stderr)
    raise SystemExit(1)

print(f"release-truth: {latest} is one version across the changelog, Meson, "
      f"PROJECT_STATUS, README and {checked} What's New notes")

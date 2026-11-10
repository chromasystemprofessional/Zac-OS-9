#!/usr/bin/env python3
"""Packaging rules the build itself can't show.

- A static system unit has no [Install] section: nothing enables or starts
  it at boot. zacos9-boot-handoff.service is started on demand by
  zacos9-boot-handoff quit, and zacos9-gpe-guard.service by its timer.
  Asking deb-systemd-invoke to start either from the package scripts only
  prints "is a disabled or a static unit not running, not starting it"
  during an install or upgrade.
- The login window's zacos9-wm runs as _greetd, which has no logind
  session, so it needs seatd for the display. Without it, Log Out ends the
  session but the login window cannot start.

    test_packaging.py SOURCE-DIR
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])
rules = (root / "debian/rules").read_text()
baseline = (root / "tests/baseline/installed-files.txt").read_text().splitlines()
skip = {"build", "tests", "emulation", "macos", "uploads", "upload assets", ".git"}

# The system units the package installs: not D-Bus services, and not the
# user manager's units (those belong to dh_installsystemduser).
names = {Path(line).name for line in baseline
    if line.startswith("{prefix}/lib/systemd/system/") and
    line.endswith((".service", ".timer", ".socket", ".path", ".target"))}

def source(name):
    for path in root.rglob(name):
        rel = path.relative_to(root)
        if rel.parts[0] in skip or rel.parts[0].startswith("obj-") or not path.is_file():
            continue
        return path
    return None

failures = 0
static = 0
for name in sorted(names):
    path = source(name)
    if path is None:
        print(f"{name} is installed as a system unit but its source isn't here")
        failures += 1
        continue
    if "[Install]" in path.read_text():
        continue
    static += 1
    if f"-X{name}" not in rules:
        print(f"{path.relative_to(root)} has no [Install], but debian/rules "
            f"does not exclude it from dh_installsystemd")
        failures += 1

control = (root / "debian/control").read_text()
package = control.split("Package: zacos9\n", 1)[1]
recommends = package.split("Recommends:", 1)[1].split("\nSuggests:", 1)[0]
if "seatd" not in {entry.strip() for entry in recommends.split(",")}:
    print("debian/control does not recommend seatd for the login window")
    failures += 1

iso = (root / "iso/config/package-lists/zacos9.list.chroot").read_text().splitlines()
if "seatd" not in {line.strip() for line in iso}:
    print("the ISO package list does not include seatd")
    failures += 1

postinst = (root / "debian/zacos9.postinst").read_text()
if "_greetd" not in postinst or "video" not in postinst or "render" not in postinst:
    print("debian/zacos9.postinst does not give _greetd the seat groups")
    failures += 1

if failures:
    sys.exit(1)
print(f"ok: the {static} static system unit(s) are excluded, and the login window has seatd")

#!/usr/bin/env python3
"""The three install lists partition the files the build installs.

    test_package_split.py SOURCE-DIR

zacos9-session, zacos9-greeter and zacos9 must cover
tests/baseline/installed-files.txt exactly once: a file that lands in no
list, or in two, fails. A few known paths are checked by package so a
wholesale swap of two lists is caught too.

An install line may be a directory or contain ? where the installed path has
a space; it is expanded against the baseline before the comparison.
"""
import fnmatch
import sys
from pathlib import Path

root = Path(sys.argv[1])
baseline = {
    line.strip().replace("{prefix}", "usr").replace("{sysconfdir}", "etc")
    for line in (root / "tests/baseline/installed-files.txt").read_text().splitlines()
    if line.strip()
}
# The Plymouth theme has its own copies of the scale art. Meson's installed
# introspection lists each source once, under share/zacos9/boot, so these six
# files are real package contents but not in installed-files.txt.
baseline |= {f"usr/share/plymouth/themes/zacos9/happy-zac-{n}x.png"
             for n in range(1, 7)}


def install_list(name):
    path = root / "debian" / name
    return [
        line.strip()
        for line in path.read_text().splitlines()
        if line.strip() and not line.strip().startswith("#")
    ]


def expand(patterns):
    """The baseline paths an install line names.

    A plain path is itself. A directory or a path with ? matches the baseline
    entries below it, because dh_install copies a directory's contents.
    """
    paths = set()
    for pattern in patterns:
        if pattern in baseline:
            paths.add(pattern)
            continue
        matches = {path for path in baseline if fnmatch.fnmatchcase(path, pattern)}
        if not matches:
            matches = {path for path in baseline
                       if fnmatch.fnmatchcase(path, pattern + "/*")}
        if matches:
            paths |= matches
        else:
            paths.add(pattern)  # report it as not installed
    return paths


packages = {
    name: expand(install_list(name + ".install"))
    for name in ("zacos9-session", "zacos9-greeter", "zacos9")
}

seen = {}
duplicates = []
for package, paths in packages.items():
    for path in paths:
        if path in seen:
            duplicates.append(f"{path}: {seen[path]} and {package}")
        seen[path] = package

missing = sorted(baseline - set(seen))
extra = sorted(set(seen) - baseline)

failures = 0
for line in duplicates:
    print(f"in two packages: {line}")
    failures += 1
for path in missing:
    print(f"in no package: {path}")
    failures += 1
for path in extra:
    print(f"not installed: {path}")
    failures += 1

known = {
    "usr/share/wayland-sessions/zacos9.desktop": "zacos9-session",
    "usr/bin/zacos9-wm": "zacos9-session",
    "usr/share/zacos9/boot/happy-zac-1x.png": "zacos9-session",
    "usr/share/zacos9/Internet Search Sites/Google.searchsite": "zacos9-session",
    "usr/libexec/zacos9/zacos9-greeter": "zacos9-greeter",
    "usr/libexec/zacos9/zacos9-login": "zacos9-greeter",
    "etc/grub.d/09_zacos9": "zacos9",
    "usr/libexec/zacos9/zacos9-install": "zacos9",
    "usr/share/plymouth/themes/zacos9/happy-zac-1x.png": "zacos9",
    "usr/share/plymouth/themes/zacos9/zacos9.plymouth": "zacos9",
}
for path, package in known.items():
    if seen.get(path) != package:
        print(f"{path} is in {seen.get(path) or 'no package'}, expected {package}")
        failures += 1

if failures:
    sys.exit(1)
print("ok: " + ", ".join(f"{name} {len(paths)}" for name, paths in packages.items()) +
      f"; all {len(baseline)} installed files accounted for")

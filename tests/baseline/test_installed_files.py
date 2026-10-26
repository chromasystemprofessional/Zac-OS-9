#!/usr/bin/env python3
"""What the build installs, against tests/baseline/installed-files.txt: a
file that drops out of the package (moved, renamed, its install() lost)
fails, and so does a new one until the list says so. Paths are relative to
the install prefix ({prefix}) or sysconfdir ({sysconfdir}), so a developer
build (/usr/local) and the package (/usr, /etc) give the same list.

    test_installed_files.py BUILDDIR BASELINE           check
    test_installed_files.py BUILDDIR BASELINE --update  write the list
"""
import json
import subprocess
import sys
from pathlib import Path

build, baseline = Path(sys.argv[1]), Path(sys.argv[2])


def introspect(what):
    return json.loads(subprocess.run(["meson", "introspect", str(build), what],
                                     capture_output=True, text=True, check=True).stdout)


options = {o["name"]: o["value"] for o in introspect("--buildoptions")}
prefix = options["prefix"].rstrip("/")
sysconfdir = options["sysconfdir"]
if not sysconfdir.startswith("/"):
    sysconfdir = f"{prefix}/{sysconfdir}"


def relative(path):
    if path.startswith(sysconfdir + "/"):
        return "{sysconfdir}" + path[len(sysconfdir):]
    if path.startswith(prefix + "/"):
        return "{prefix}" + path[len(prefix):]
    return path


installed = sorted({relative(p) for p in introspect("--installed").values()})
if "--update" in sys.argv[3:]:
    baseline.write_text("\n".join(installed) + "\n")
    print(f"{baseline}: {len(installed)} files")
    sys.exit(0)

expected = [line for line in baseline.read_text().splitlines() if line]
gone = sorted(set(expected) - set(installed))
new = sorted(set(installed) - set(expected))
for path in gone:
    print(f"no longer installed: {path}")
for path in new:
    print(f"newly installed: {path}")
if gone or new:
    print(f"\n{len(installed)} files installed, {len(expected)} in {baseline.name}. If that is meant, "
          f"update the list:\n  python3 tests/baseline/test_installed_files.py BUILDDIR {baseline} --update")
    sys.exit(1)
print(f"ok: the {len(installed)} installed files are the baseline's")

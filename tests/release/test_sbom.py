#!/usr/bin/env python3
"""The release SBOM: packages and GPL source, deterministic output.

usage: test_sbom.py SOURCE_ROOT
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

root = Path(sys.argv[1])
script = root / "scripts" / "build-sbom.py"
if not shutil.which("dpkg-deb"):
    print("skip: dpkg-deb isn't installed")
    sys.exit(77)

with tempfile.TemporaryDirectory() as d:
    work = Path(d)
    package = work / "pkg"
    (package / "DEBIAN").mkdir(parents=True)
    (package / "DEBIAN" / "control").write_text(
        "Package: zacos9-test\n"
        "Version: 1.0.0\n"
        "Architecture: all\n"
        "Maintainer: Test <test@example.com>\n"
        "Depends: libc6 (>= 2.38)\n"
        "Description: a test package\n")
    deb = work / "zacos9-test_1.0.0_all.deb"
    subprocess.run(["dpkg-deb", "--root-owner-group", "--build",
                    str(package), str(deb)], check=True, capture_output=True)

    (work / "README").write_text("source\n")
    source = work / "zacos9-emulators_0~git20260101.abc1234.source.tar.gz"
    with tarfile.open(source, "w:gz") as tar:
        tar.add(work / "README", arcname="README")

    env = {**os.environ, "SOURCE_DATE_EPOCH": "1700000000"}
    command = [sys.executable, str(script), "--version", "1.0.0",
               "--commit", "deadbeef", str(deb), str(source)]
    first = subprocess.run(command, check=True, capture_output=True, text=True,
                           env=env).stdout
    second = subprocess.run(command, check=True, capture_output=True, text=True,
                            env=env).stdout
    if first != second:
        raise SystemExit("sbom: output isn't deterministic")

    sbom = json.loads(first)
    if sbom["specVersion"] != "1.6" or sbom["bomFormat"] != "CycloneDX":
        raise SystemExit("sbom: wrong format or spec version")
    if sbom["metadata"]["timestamp"] != "2023-11-14T22:13:20Z":
        raise SystemExit("sbom: SOURCE_DATE_EPOCH isn't used")
    if sbom["metadata"]["component"]["name"] != "ZacOS 9":
        raise SystemExit("sbom: wrong root component")
    components = {c["name"]: c for c in sbom["components"]}
    if "zacos9-test" not in components or components["zacos9-test"]["type"] != "application":
        raise SystemExit("sbom: the package component is missing")
    if components["zacos9-test"]["hashes"][0]["content"] != hashlib.sha256(deb.read_bytes()).hexdigest():
        raise SystemExit("sbom: the package hash is wrong")
    if components["zacos9-test"]["properties"][0]["value"] != "libc6 (>= 2.38)":
        raise SystemExit("sbom: the package depends field is missing")
    if components["zacos9-test"]["licenses"][0]["license"]["id"] != "GPL-3.0-or-later":
        raise SystemExit("sbom: wrong package license")
    if "zacos9-emulators" not in components or components["zacos9-emulators"]["type"] != "file":
        raise SystemExit("sbom: the GPL source component is missing")
    if components["zacos9-emulators"]["licenses"][0]["license"]["id"] != "GPL-2.0-or-later":
        raise SystemExit("sbom: wrong emulator license")
    refs = sbom["dependencies"][0]["dependsOn"]
    if sorted(refs) != sorted(c["bom-ref"] for c in sbom["components"]):
        raise SystemExit("sbom: the dependency graph is incomplete")

print("sbom: ok")

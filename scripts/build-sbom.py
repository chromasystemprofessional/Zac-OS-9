#!/usr/bin/env python3
"""Build a CycloneDX SBOM for a ZacOS 9 release.

    build-sbom.py [--version VERSION] [--commit COMMIT] [--release-url URL] FILE...
    build-sbom.py --version 1.18.35 build/packages/zacos9*.deb

The component list is the release's packages plus the emulators' GPL source
tarball. Dependencies on the Debian base system are recorded as the
package's Depends field, not resolved into a full distribution graph; this
is an SBOM of what ZacOS 9 publishes, not of the whole ISO.

Output is deterministic: components are sorted, the serial number comes from
the release URL, and the timestamp comes from SOURCE_DATE_EPOCH when set.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path

REPO_URL = "https://github.com/chromasystemprofessional/Zac-OS-9"
PROJECT_URL = "https://chromasystemprofessional.com/zacos9/"


def deb_field(path, name):
    result = subprocess.run(
        ["dpkg-deb", "-f", str(path), name],
        capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else ""


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def license_for(package):
    # The emulators are GPL-2; ZacOS's own packages are GPL-3.0-or-later.
    if package == "zacos9-emulators":
        return "GPL-2.0-only"
    return "GPL-3.0-or-later"


def package_component(path):
    package = deb_field(path, "Package")
    version = deb_field(path, "Version")
    arch = deb_field(path, "Architecture")
    if not package or not version:
        raise SystemExit(f"build-sbom: {path} has no Package or Version field")
    ref = f"pkg:deb/debian/{package}@{version}?arch={arch}"
    component = {
        "type": "application",
        "bom-ref": ref,
        "name": package,
        "version": version,
        "licenses": [{"license": {"id": license_for(package)}}],
        "purl": ref,
        "hashes": [{"alg": "SHA-256", "content": sha256(path)}],
    }
    depends = deb_field(path, "Depends")
    if depends:
        component["properties"] = [
            {"name": "zacos9:depends", "value": depends},
        ]
    return component


def source_component(path):
    name = path.name
    stem = name[:-len(".source.tar.gz")]
    package, _, version = stem.partition("_")
    if not package or not version:
        raise SystemExit(f"build-sbom: can't read a package and version from {name}")
    return {
        "type": "file",
        "bom-ref": f"urn:zacos9:source:{package}:{version}",
        "name": package,
        "version": version,
        "licenses": [{"license": {"id": "GPL-2.0-only"}}],
        "hashes": [{"alg": "SHA-256", "content": sha256(path)}],
        "properties": [
            {"name": "zacos9:source", "value": "GPL source tarball for the emulators"},
        ],
    }


def project_version(root):
    result = subprocess.run(
        ["dpkg-parsechangelog", "-S", "Version"],
        cwd=root, capture_output=True, text=True)
    if result.returncode != 0 or not result.stdout.strip():
        raise SystemExit("build-sbom: no --version and no debian/changelog")
    return result.stdout.strip()


def git_commit(root):
    result = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "HEAD"],
        capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else ""


def main():
    parser = argparse.ArgumentParser(description="Build a CycloneDX SBOM for a ZacOS 9 release.")
    parser.add_argument("--version", help="release version (default: debian/changelog)")
    parser.add_argument("--commit", help="source commit (default: git HEAD)")
    parser.add_argument("--release-url", help="release page URL")
    parser.add_argument("files", nargs="+", type=Path, help="release .deb and source files")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    version = args.version or project_version(root)
    commit = args.commit if args.commit is not None else git_commit(root)
    release_url = args.release_url or f"{REPO_URL}/releases/tag/v{version}"

    epoch = os.environ.get("SOURCE_DATE_EPOCH", "").strip()
    if epoch:
        timestamp = datetime.fromtimestamp(int(epoch), timezone.utc)
    else:
        timestamp = datetime.fromtimestamp(int(time.time()), timezone.utc)
    timestamp = timestamp.strftime("%Y-%m-%dT%H:%M:%SZ")

    components = []
    for path in sorted(set(args.files)):
        if not path.is_file():
            raise SystemExit(f"build-sbom: {path} isn't a file")
        if path.name.endswith("-dbgsym.deb") or "-dbgsym_" in path.name:
            continue
        if path.suffix == ".deb":
            components.append(package_component(path))
        elif path.name.endswith(".source.tar.gz"):
            components.append(source_component(path))
        else:
            raise SystemExit(f"build-sbom: don't know what to do with {path}")

    if not components:
        raise SystemExit("build-sbom: no release packages to describe")

    root_ref = f"pkg:generic/zacos9@{version}"
    root_component = {
        "type": "application",
        "bom-ref": root_ref,
        "name": "ZacOS 9",
        "version": version,
        "licenses": [{"license": {"id": "GPL-3.0-or-later"}}],
        "externalReferences": [
            {"type": "vcs", "url": REPO_URL},
            {"type": "website", "url": PROJECT_URL},
            {"type": "distribution", "url": release_url},
        ],
    }
    if commit:
        root_component["properties"] = [
            {"name": "zacos9:commit", "value": commit},
        ]

    sbom = {
        "bomFormat": "CycloneDX",
        "specVersion": "1.6",
        "serialNumber": "urn:uuid:" + str(uuid.uuid5(uuid.NAMESPACE_URL, release_url)),
        "version": 1,
        "metadata": {
            "timestamp": timestamp,
            "tools": {
                "components": [
                    {
                        "type": "application",
                        "name": "build-sbom.py",
                        "publisher": "ZacOS",
                    },
                ],
            },
            "component": root_component,
        },
        "components": components,
        "dependencies": [
            {
                "ref": root_ref,
                "dependsOn": [component["bom-ref"] for component in components],
            },
        ],
    }
    json.dump(sbom, sys.stdout, indent=2, sort_keys=True)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()

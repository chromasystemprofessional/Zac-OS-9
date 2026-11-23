#!/usr/bin/env python3
"""Build the one-release monolithic zacos9 package that bridges the 1.18.28
package split.

The updater on 1.18.27 and 1.18.29 downloads only zacos9_*.deb. Since the
split, that metapackage needs zacos9-session and zacos9-greeter, so those
updaters cannot install a newer release without the signed repository. This
package has no dependencies on the other two: it carries their files and runs
their maintainer scripts itself. Once installed it also carries the fixed
1.18.30 updater, which downloads the whole package set for later releases.

The next normal release must build its split packages with
Breaks/Replaces: zacos9 (<< that version), so they can replace this package.

    scripts/build-transition-package.py \
        --version 1.18.31 --base-version 1.18.30 \
        --debs build/packages --output build/packages

It takes zacos9, zacos9-session and zacos9-greeter of --base-version from
--debs and writes zacos9_<version>_<arch>.deb to --output.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

PACKAGES = ("zacos9-session", "zacos9-greeter", "zacos9")


def run(*args):
    subprocess.run(args, check=True)


def parse_control(path):
    fields = []
    for line in path.read_text().splitlines():
        if line[:1] in (" ", "\t") and fields:
            fields[-1][1] += "\n" + line
        else:
            key, sep, value = line.partition(":")
            if sep:
                fields.append([key, value.strip()])
    return fields


def control_dict(fields):
    return {key: value for key, value in fields}


def write_control(path, fields, order):
    values = control_dict(fields)
    lines = []
    for key in order:
        if key in values:
            lines.append(f"{key}: {values[key]}")
    for key, value in fields:
        if key not in order:
            lines.append(f"{key}: {value}")
    path.write_text("\n".join(lines) + "\n")


def dependency_name(item):
    return re.match(r"([A-Za-z0-9+.:-]+)", item).group(1).split(":")[0]


def dependency_version(item):
    match = re.search(r"\(\s*(?:>=|>>|=|<=|<<)?\s*([^)]+)\)", item)
    return match.group(1).strip() if match else ""


def stricter(old, new):
    """Keep the dependency with the higher version; a constrained one wins
    over an unconstrained one."""
    old_version, new_version = dependency_version(old), dependency_version(new)
    if not old_version:
        return new
    if not new_version:
        return old
    result = subprocess.run(["dpkg", "--compare-versions", new_version, "gt", old_version])
    return new if result.returncode == 0 else old


def merge_list(values):
    by_name = {}
    for value in values:
        for item in value.replace("\n", " ").split(","):
            item = item.strip()
            if not item:
                continue
            name = dependency_name(item)
            by_name[name] = stricter(by_name[name], item) if name in by_name else item
    return ", ".join(by_name.values())


def source_files(root):
    return {
        path.relative_to(root): path
        for path in root.rglob("*")
        if path.is_file() or path.is_symlink()
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", required=True, help="transition package version, e.g. 1.18.31")
    parser.add_argument("--base-version", required=True, help="split package version to bundle, e.g. 1.18.30")
    parser.add_argument("--debs", type=Path, required=True, help="directory holding the three base .debs")
    parser.add_argument("--output", type=Path, default=Path("build/packages"))
    args = parser.parse_args()

    temp = Path(tempfile.mkdtemp(prefix="zacos9-transition-"))
    try:
        roots = {}
        controls = {}
        for package in PACKAGES:
            deb = args.debs / f"{package}_{args.base_version}_amd64.deb"
            if not deb.is_file():
                sys.exit(f"missing {deb}")
            data = temp / package
            control = temp / (package + "-control")
            data.mkdir()
            control.mkdir()
            run("dpkg-deb", "-x", str(deb), str(data))
            run("dpkg-deb", "-e", str(deb), str(control))
            roots[package] = data
            controls[package] = control

        # Every split package must carry the same file with the same contents
        # if it appears more than once.
        seen = {}
        for package in PACKAGES:
            for relative, path in source_files(roots[package]).items():
                digest = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else "symlink"
                if relative in seen and seen[relative][0] != digest:
                    sys.exit(f"conflicting file in the split packages: {relative}")
                seen[relative] = (digest, package)

        root = temp / "root"
        root.mkdir()
        for package in PACKAGES:
            run("dpkg-deb", "-x", str(args.debs / f"{package}_{args.base_version}_amd64.deb"), str(root))

        # The split maintainer scripts stay in the package and run from the
        # monolith's postinst, which is configuring the same files. prerm and
        # postrm are copied into one script because their files may already
        # be gone when an upgrade removes the transition package.
        transition = root / "usr/share/zacos9/transition"
        transition.mkdir(parents=True)
        for name in ("session", "greeter", "main"):
            source = controls["zacos9" if name == "main" else "zacos9-" + name] / "postinst"
            target = transition / f"{name}.postinst"
            shutil.copy2(source, target)
            target.chmod(0o755)

        debian = root / "DEBIAN"
        debian.mkdir()

        postinst = """#!/bin/sh
set -e
dir=/usr/share/zacos9/transition
case "$1" in
configure|abort-upgrade|abort-remove|abort-deconfigure)
	for name in session greeter main; do
		script="$dir/$name.postinst"
		if [ -x "$script" ]; then
			"$script" "$@"
		fi
	done
	;;
triggered)
	for name in session main; do
		script="$dir/$name.postinst"
		if [ -x "$script" ]; then
			"$script" "$@"
		fi
	done
	;;
esac
exit 0
"""
        (debian / "postinst").write_text(postinst)
        (debian / "postinst").chmod(0o755)

        for name, order in (("prerm", ("main", "session", "greeter")),
                            ("postrm", ("main", "session", "greeter"))):
            parts = ["#!/bin/sh", "set -e"]
            for part in order:
                source = controls["zacos9" if part == "main" else "zacos9-" + part] / name
                if not source.exists():
                    continue
                body = source.read_text().splitlines()
                if body and body[0].startswith("#!"):
                    body = body[1:]
                parts.extend(body)
            parts.append("exit 0")
            (debian / name).write_text("\n".join(parts) + "\n")
            (debian / name).chmod(0o755)

        base = control_dict(parse_control(controls["zacos9"] / "control"))
        all_controls = [control_dict(parse_control(controls[package] / "control"))
                        for package in PACKAGES]
        drop = {"zacos9-session", "zacos9-greeter"}
        depends = merge_list([c.get("Depends", "") for c in all_controls])
        depends = ", ".join(item for item in depends.split(", ")
                            if dependency_name(item) not in drop)
        recommends = merge_list([c.get("Recommends", "") for c in all_controls])
        suggests = merge_list([c.get("Suggests", "") for c in all_controls])

        fields = [
            ("Package", "zacos9"),
            ("Version", args.version),
            ("Architecture", "amd64"),
            ("Maintainer", base.get("Maintainer", "")),
            ("Installed-Size", str(sum(int(c.get("Installed-Size", "0")) for c in all_controls))),
            ("Depends", depends),
        ]
        if recommends:
            fields.append(("Recommends", recommends))
        if suggests:
            fields.append(("Suggests", suggests))
        fields += [
            ("Breaks", f"zacos9-session (<< {args.version}), zacos9-greeter (<< {args.version})"),
            ("Replaces", f"zacos9-session (<< {args.version}), zacos9-greeter (<< {args.version})"),
            ("Section", base.get("Section", "x11")),
            ("Priority", base.get("Priority", "optional")),
        ]
        for key in ("Homepage", "Description"):
            if key in base:
                fields.append((key, base[key]))
        write_control(debian / "control", fields, [key for key, _ in fields])

        existing = []
        for package in PACKAGES:
            conffiles = controls[package] / "conffiles"
            if conffiles.exists():
                for line in conffiles.read_text().splitlines():
                    if line.strip() and line not in existing:
                        existing.append(line)
        if existing:
            (debian / "conffiles").write_text("\n".join(existing) + "\n")

        triggers = []
        for package in PACKAGES:
            path = controls[package] / "triggers"
            if path.exists():
                for line in path.read_text().splitlines():
                    if line.strip() and line not in triggers:
                        triggers.append(line)
        if triggers:
            (debian / "triggers").write_text("\n".join(triggers) + "\n")

        sums = []
        for relative, path in sorted(source_files(root).items()):
            if path.is_symlink():
                continue
            digest = hashlib.md5(path.read_bytes()).hexdigest()
            sums.append(f"{digest}  {relative.as_posix()}")
        (debian / "md5sums").write_text("\n".join(sums) + "\n")

        output = args.output / f"zacos9_{args.version}_amd64.deb"
        args.output.mkdir(parents=True, exist_ok=True)
        run("dpkg-deb", "--build", "--root-owner-group", str(root), str(output))
        print(output)
    finally:
        shutil.rmtree(temp, ignore_errors=True)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Build a signed flat repository and use it with apt.

    test_repo.py BUILD-REPO-SH

The test makes a temporary signing key and two tiny .deb files, builds the
repository twice, and checks that the second run keeps the first package. It
verifies InRelease and Release.gpg, then runs apt-get update and apt-get
download against a file:// source, so the layout, hashes and keyring are
exercised together.
"""
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

script = Path(sys.argv[1]).resolve()
required = ["apt-ftparchive", "apt-get", "dpkg-deb", "gpg", "gpgv", "gzip"]
missing = [name for name in required if shutil.which(name) is None]
if missing:
    print("SKIP: missing " + ", ".join(missing))
    sys.exit(77)


def run(args, env=None, cwd=None):
    return subprocess.run(args, env=env, cwd=cwd, check=True,
                          capture_output=True, text=True)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def make_deb(directory, version):
    tree = directory / f"tree-{version}"
    (tree / "DEBIAN").mkdir(parents=True)
    (tree / "DEBIAN" / "control").write_text(
        "Package: zacos9-dummy\n"
        f"Version: {version}\n"
        "Architecture: all\n"
        "Maintainer: ZacOS Test <test@example.com>\n"
        "Section: misc\n"
        "Priority: optional\n"
        "Description: dummy package for the repository test\n"
        " A package with no dependencies, used only to exercise the repository.\n")
    package = directory / f"zacos9-dummy_{version}_all.deb"
    run(["dpkg-deb", "--root-owner-group", "--build", str(tree), str(package)])
    return package


def build_repo(repo, packages, key, env):
    run([str(script), "--packages", str(packages), "--output", str(repo),
         "--key", key, "--suite", "test", "--codename", "test"], env=env)


def verify(repo, packages, env):
    assert (repo / "InRelease").is_file()
    assert (repo / "Release.gpg").is_file()
    assert (repo / "Packages.gz").is_file()
    assert (repo / "zacos9-archive-keyring.gpg").is_file()
    run(["gpgv", "--keyring", str(repo / "zacos9-archive-keyring.gpg"),
         str(repo / "InRelease")], env=env)
    run(["gpgv", "--keyring", str(repo / "zacos9-archive-keyring.gpg"),
         str(repo / "Release.gpg"), str(repo / "Release")], env=env)

    index = (repo / "Packages").read_text()
    release = (repo / "Release").read_text()
    for package in packages:
        assert f"Filename: ./{package.name}" in index, package.name
        assert f"SHA256: {sha256(package)}" in index, package.name
    assert "Packages" in release and "Packages.gz" in release
    assert not any(line.endswith(" Release") for line in release.splitlines()), \
        "Release lists itself"


def apt_test(repo, env):
    apt = repo.parent / "apt"
    (apt / "etc").mkdir(parents=True)
    (apt / "state" / "lists" / "partial").mkdir(parents=True)
    (apt / "cache" / "archives" / "partial").mkdir(parents=True)
    (apt / "download").mkdir()
    (apt / "etc" / "sources.list").write_text(
        f"deb [signed-by={repo / 'zacos9-archive-keyring.gpg'}] "
        f"file:{repo} ./\n")
    common = [
        "-o", "Dir::Etc::main=/dev/null",
        "-o", "Dir::Etc::parts=-",
        "-o", f"Dir::Etc::sourcelist={apt / 'etc' / 'sources.list'}",
        "-o", "Dir::Etc::sourceparts=-",
        "-o", f"Dir::State::lists={apt / 'state' / 'lists'}",
        "-o", f"Dir::Cache={apt / 'cache'}",
        "-o", "Dir::State::status=/dev/null",
        "-o", "APT::Get::List-Cleanup=0",
        "-o", "APT::Sandbox::User=",
    ]
    run(["apt-get", *common, "update"], env=env)
    run(["apt-get", *common, "download", "zacos9-dummy"],
        env=env, cwd=apt / "download")
    assert (apt / "download" / "zacos9-dummy_1.0_all.deb").is_file()


def main():
    temp = Path(tempfile.mkdtemp(prefix="zacos9-repo-"))
    try:
        gnupg = temp / "gnupg"
        gnupg.mkdir(mode=0o700)
        env = os.environ.copy()
        env["GNUPGHOME"] = str(gnupg)
        env["ZACOS9_REPO_GPG_PASSPHRASE"] = ""
        run(["gpg", "--batch", "--pinentry-mode", "loopback", "--passphrase", "",
             "--quick-generate-key", "ZacOS Repo Test <test@example.com>",
             "default", "default", "never"], env=env)
        keys = run(["gpg", "--batch", "--with-colons", "--list-keys"],
                   env=env).stdout
        fingerprint = next(line.split(":")[9] for line in keys.splitlines()
                           if line.startswith("fpr:"))

        packages = temp / "packages"
        packages.mkdir()
        first = make_deb(temp, "1.0")
        shutil.copy(first, packages)
        repo = temp / "repo"
        build_repo(repo, packages, fingerprint, env)
        verify(repo, [first], env)
        apt_test(repo, env)

        second = make_deb(temp, "2.0")
        shutil.copy(second, packages)
        build_repo(repo, packages, fingerprint, env)
        verify(repo, [first, second], env)
        index = (repo / "Packages").read_text()
        assert "Version: 1.0" in index and "Version: 2.0" in index, \
            "the second build dropped the first package"

        print("ok: signed repository, apt update and download, "
              "old package kept")
    finally:
        shutil.rmtree(temp)


if __name__ == "__main__":
    main()

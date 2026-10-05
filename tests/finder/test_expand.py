"""zacos9-expand: archives expanded in place (shell/finder/zacos9-expand).

    python3 tests/finder/test_expand.py shell/finder/zacos9-expand
"""
import io
import base64
import json
import os
import runpy
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
import zipfile
import libarchive
from unittest.mock import patch

TOOL = os.path.abspath(sys.argv[1])
fails = 0


def check(ok, what):
    global fails
    print(("ok    " if ok else "FAIL  ") + what)
    fails += not ok


def expand(path, env=None):
    r = subprocess.run([sys.executable, TOOL, path], capture_output=True, text=True,
                       timeout=30, env=env)
    return r.returncode, r.stdout.strip(), r.stderr.strip()


def seven_zip(path, entries):
    with libarchive.file_writer(path, "7zip") as archive:
        for name, data, mode in entries:
            archive.add_file_from_memory(name, len(data), data, permission=mode)


def stuffit(path, entries):
    """Original, uncompressed StuffIt 1 fixture with data and resource forks."""
    def crc(data):
        value = 0
        for byte in data:
            value ^= byte
            for _ in range(8):
                value = (value >> 1) ^ (0xa001 if value & 1 else 0)
        return value

    body = bytearray()
    for name, data, fork in entries:
        name = name.encode("mac_roman")
        header = (bytes([0, 0, len(name)]) + name + bytes(63 - len(name))
                  + b"TEXTttxt"
                  + struct.pack(">HIIIIIIHH", 0, 0, 0, len(fork), len(data),
                                len(fork), len(data), crc(fork), crc(data))
                  + bytes(6))
        body += header + struct.pack(">H", crc(header)) + fork + data
    with open(path, "wb") as output:
        output.write(b"SIT!" + struct.pack(">HI", len(entries), 22 + len(body))
                     + b"rLau" + bytes([1]) + bytes(7) + body)


with tempfile.TemporaryDirectory(prefix=".expand-tests-", dir=os.path.dirname(__file__)) as d:
    j = lambda *p: os.path.join(d, *p)

    with zipfile.ZipFile(j("Project.zip"), "w") as z:
        z.writestr("Project/readme.txt", "hi")
        z.writestr("Project/src/main.c", "int main(){}")
        z.writestr("__MACOSX/Project/._readme.txt", "junk")
        z.writestr("Project/._readme.txt", "junk")
        zi = zipfile.ZipInfo("Project/run.sh")
        zi.external_attr = 0o755 << 16
        z.writestr(zi, "#!/bin/sh\n")
    code, out, _ = expand(j("Project.zip"))
    check(code == 0 and out == j("Project"), "one top-level folder lands beside the archive")
    check(os.path.isfile(j("Project", "src", "main.c")), "its contents are all there")
    check(os.access(j("Project", "run.sh"), os.X_OK), "a zip's executable bit is kept")
    check(not os.path.exists(j("__MACOSX")) and not os.path.exists(j("Project", "._readme.txt")),
          "macOS's __MACOSX and ._ files are left out")
    check(os.path.isfile(j("Project.zip")), "the archive is kept")

    with zipfile.ZipFile(j("Photos.zip"), "w") as z:
        z.writestr("a.jpg", "1")
        z.writestr("b.jpg", "2")
    code, out, _ = expand(j("Photos.zip"))
    check(code == 0 and out == j("Photos") and sorted(os.listdir(out)) == ["a.jpg", "b.jpg"],
          "several top-level items go into a folder named after the archive")
    code, out, _ = expand(j("Photos.zip"))
    check(code == 0 and out == j("Photos 2"), "a name already taken gets a number, nothing overwritten")

    with zipfile.ZipFile(j("Single.zip"), "w") as z:
        z.writestr("notes.txt", "x")
    open(j("notes.txt"), "w").write("mine")
    code, out, _ = expand(j("Single.zip"))
    check(code == 0 and out == j("notes 2.txt") and open(j("notes.txt")).read() == "mine",
          "a single file beside an existing one keeps its extension after the number")

    with zipfile.ZipFile(j("Evil.zip"), "w") as z:
        z.writestr("../escaped.txt", "bad")
    code, _, err = expand(j("Evil.zip"))
    check(code != 0 and not os.path.exists(os.path.join(os.path.dirname(d), "escaped.txt")),
          "an entry that would land outside the folder is refused")
    check("outside" in err, "and says why")

    with tarfile.open(j("Tools.tar.gz"), "w:gz") as t:
        ti = tarfile.TarInfo("tools/x.txt")
        ti.size = 1
        t.addfile(ti, io.BytesIO(b"x"))
    code, out, _ = expand(j("Tools.tar.gz"))
    check(code == 0 and os.path.isfile(os.path.join(out, "x.txt")), ".tar.gz expands")

    open(j("Broken.zip"), "wb").write(b"PK\x03\x04 not really")
    code, _, err = expand(j("Broken.zip"))
    check(code != 0 and "damaged" in err, "a damaged zip says so")

    seven_zip(j("Seven.7z"), [
        ("Seven/readme.txt", b"hello", 0o644),
        ("Seven/run.sh", b"#!/bin/sh\n", 0o755),
        ("__MACOSX/._readme.txt", b"junk", 0o644),
        ("Seven/._readme.txt", b"junk", 0o644),
        ("Seven/.DS_Store", b"junk", 0o644),
    ])
    code, out, err = expand(j("Seven.7z"))
    check(code == 0 and out == j("Seven"), "7z's single folder is extracted in place")
    check(os.path.isfile(j("Seven", "readme.txt")) and
          open(j("Seven", "readme.txt"), "rb").read() == b"hello",
          "7z file contents are intact")
    check(os.access(j("Seven", "run.sh"), os.X_OK), "7z executable permission is kept")
    check(not os.path.exists(j("__MACOSX")) and
          not os.path.exists(j("Seven", "._readme.txt")) and
          not os.path.exists(j("Seven", ".DS_Store")), "7z macOS metadata is omitted")
    check(os.path.isfile(j("Seven.7z")), "the original 7z archive is kept")
    code, out, _ = expand(j("Seven.7z"))
    check(code == 0 and out == j("Seven 2"), "7z folder collisions use a numbered name")

    seven_zip(j("Bundle.7Z"), [("one.txt", b"1", 0o644), ("two.txt", b"2", 0o644)])
    code, out, _ = expand(j("Bundle.7Z"))
    check(code == 0 and out == j("Bundle") and sorted(os.listdir(out)) == ["one.txt", "two.txt"],
          "multiple 7z items use the archive name, including uppercase .7Z")
    seven_zip(j("One.7z"), [("notes.txt", b"new", 0o644)])
    code, out, _ = expand(j("One.7z"))
    check(code == 0 and out == j("notes 3.txt") and open(j("notes.txt")).read() == "mine",
          "7z single files preserve extensions and never overwrite existing files")

    for index, name in enumerate(("../escaped-7z.txt", "/escaped-7z.txt",
                                   "..\\escaped-7z.txt", "C:\\escaped-7z.txt")):
        path = j(f"Unsafe{index}.7z")
        seven_zip(path, [("safe.txt", b"first", 0o644), (name, b"bad", 0o644)])
        code, out, err = expand(path)
        check(code != 0 and not out and "outside" in err,
              f"7z refuses unsafe path {name!r} without publishing partial contents")
    check(not os.path.exists(os.path.join(os.path.dirname(d), "escaped-7z.txt")),
          "7z path traversal writes nothing beside the extraction directory")

    os.symlink("../outside", j("fixture-link"))
    with libarchive.file_writer(j("Link.7z"), "7zip") as archive:
        archive.add_files(j("fixture-link"))
    code, out, err = expand(j("Link.7z"))
    check(code != 0 and not out and "link" in err, "7z refuses archive-provided symbolic links")
    seven_zip(j("Empty.7z"), [])
    code, out, err = expand(j("Empty.7z"))
    check(code != 0 and not out and "empty" in err, "an empty 7z reports an error")
    open(j("Broken.7z"), "wb").write(b"7z\xbc\xaf\x27\x1c not really")
    code, out, err = expand(j("Broken.7z"))
    check(code != 0 and not out and "couldn't be expanded" in err, "a damaged 7z reports an error")
    # Original fixture: secret.txt contains "private test data\n", encrypted
    # with 7-Zip using the test-only password "test-password".
    encrypted = (
        "N3q8ryccAARWOeXMoAAAAAAAAAAvAAAAAAAAAKKY1e9jh/RWTNcbzmiszyoInVsUnARGkJYW"
        "tRNNSe/ps2uDOFyodbjC+NA7KRnNd459k8p8A1Q575vIArAl0dnO03hK2dweQk6s/Vx0"
        "y6CuSLa6gbdW2RiTW/4UnSCa+UI4tPIepZR8dIkhFhZC7urE1v5riNEAjAXWAJVrju/L"
        "0wbUxBaeQK/N9aCrjvbipyX45jUzOA3oWfqx7NhvQkX3WjOZFwYgAQmAgAAHCwEAASQG"
        "8QcBElMPY0H/CjdFyLtnvYAwAHjixgxyCgHcaLe2AAA="
    )
    open(j("Encrypted.7z"), "wb").write(base64.b64decode(encrypted))
    code, out, err = expand(j("Encrypted.7z"))
    check(code != 0 and not out and "password" in err,
          "an encrypted 7z reports unsupported password protection without hanging")

    # Real decoder integration uses only original, generated test data.
    if shutil.which("unar") and shutil.which("lsar"):
        nested = io.BytesIO()
        with zipfile.ZipFile(nested, "w") as archive:
            archive.writestr("inside.txt", "leave archived")
        stuffit(j("Original.SIT"), [
            ("Original.txt", b"original data\n", b"original fork\n"),
            ("OnlyTheme", b"", b"original resource-only theme\n"),
            ("nested.zip", nested.getvalue(), b""),
            ("website:theme", b"original colon filename\n", b""),
        ])
        code, out, err = expand(j("Original.SIT"))
        check(code == 0 and out == j("Original"), "original StuffIt fixture expands in place")
        if code == 0:
            check(open(j("Original", "Original.txt"), "rb").read() == b"original data\n",
                  "StuffIt original data fork is intact")
            sidecar = open(j("Original", "._Original.txt"), "rb").read()
            check(sidecar[:4] == b"\x00\x05\x16\x07" and b"original fork\n" in sidecar,
                  "StuffIt resource fork is preserved as hidden AppleDouble")
            check(b"original resource-only theme\n" in
                  open(j("Original", "._OnlyTheme"), "rb").read(),
                  "StuffIt resource-only entries retain their resource fork")
            check(os.path.isfile(j("Original", "OnlyTheme")) and
                  os.path.getsize(j("Original", "OnlyTheme")) == 0,
                  "StuffIt resource-only entries gain visible empty data forks")
            check(open(j("Original", "nested.zip"), "rb").read() == nested.getvalue()
                  and not os.path.exists(j("Original", "inside.txt")),
                  "StuffIt does not recursively extract nested ZIP archives")
            check(any(open(os.path.join(folder, name), "rb").read()
                      == b"original colon filename\n"
                      for folder, _, files in os.walk(j("Original"))
                      for name in files if not name.startswith("._")),
                  "legal Mac colon names are accepted without losing file contents")
        code, out, err = expand(j("Original.SIT"))
        check(code == 0 and out == j("Original 2"), "StuffIt folder collisions are numbered")
        stuffit(j("Theme.sit"), [("VisibleTheme", b"", b"original theme resource\n")])
        code, out, err = expand(j("Theme.sit"))
        check(code == 0 and out == j("Theme") and
              sorted(os.listdir(out)) == ["._VisibleTheme", "VisibleTheme"] and
              os.path.getsize(j("Theme", "VisibleTheme")) == 0,
              "a resource-only StuffIt theme publishes both forks together")
        stuffit(j("Single.sit"), [("single-sit.txt", b"single\n", b"")])
        code, out, err = expand(j("Single.sit"))
        check(code == 0 and out == j("single-sit.txt"),
              "StuffIt single data-only file uses its own name")
        code, out, err = expand(j("Single.sit"))
        check(code == 0 and out == j("single-sit 2.txt"),
              "StuffIt single-file collisions preserve extensions")
        stuffit(j("Traversal.sit"), [("../outside-sit.txt", b"unsafe", b"")])
        code, out, err = expand(j("Traversal.sit"))
        check(code != 0 and not out and "outside" in err,
              "real StuffIt traversal is refused before extraction")
        with open(j("Broken.sit"), "wb") as output:
            output.write(b"SIT! damaged archive")
        code, out, err = expand(j("Broken.sit"))
        check(code != 0 and not out, "damaged StuffIt archives publish nothing")
    else:
        print("skip  original StuffIt decoder integration (install unar)")

    # Decoder doubles exercise preflight metadata that classic SIT cannot
    # represent, and prove rejected listings never invoke the extractor.
    mockbin = j("decoders")
    os.mkdir(mockbin)
    decoder = """#!/usr/bin/python3
import json, os, sys
args = sys.argv[1:]
assert "-nr" in args and args[args.index("-p")+1] == ""
assert sys.stdin.read() == ""
with open(args[-1]) as source:
    fixture = json.load(source)
if os.path.basename(sys.argv[0]) == "lsar":
    assert "-j" in args
    if fixture.get("broken_json"):
        print("not JSON")
    else:
        print(json.dumps(fixture["listing"]))
    sys.exit(fixture.get("listing_exit", 0))
assert args[args.index("-k")+1] == "hidden" and "-D" in args and "-f" in args
with open(os.environ["SIT_CALLED"], "w") as marker:
    marker.write("called")
root = args[args.index("-o")+1]
for name, content in fixture.get("files", {}).items():
    path = os.path.join(root, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as output:
        output.write(content)
kind = fixture.get("special")
if kind == "symlink":
    os.symlink("../outside", os.path.join(root, "link"))
elif kind == "hardlink":
    os.link(os.path.join(root, "first"), os.path.join(root, "second"))
elif kind == "fifo":
    os.mkfifo(os.path.join(root, "pipe"))
sys.exit(fixture.get("extract_exit", 0))
"""
    for command in ("lsar", "unar"):
        with open(os.path.join(mockbin, command), "w") as output:
            output.write(decoder)
        os.chmod(os.path.join(mockbin, command), 0o755)
    env = dict(os.environ, PATH=mockbin + os.pathsep + os.environ.get("PATH", ""),
               SIT_CALLED=j("called"))

    def mock_sit(entries, **options):
        listing = {"lsarFormatVersion": 2, "lsarFormatName": "StuffIt",
                   "lsarProperties": options.pop("properties", {}), "lsarContents": entries}
        fixture = dict(listing=listing, **options)
        with open(j("Mock.sit"), "w") as output:
            json.dump(fixture, output)
        if os.path.exists(j("called")):
            os.unlink(j("called"))
        return expand(j("Mock.sit"), env)

    def sit_entry(name="safe.txt", **metadata):
        return dict(XADFileName=name, XADFileSize=5, **metadata)

    for name in ("../escape", "/absolute", "..\\escape", "C:\\escape",
                 "C:escape", "\\\\server\\share", "safe/../escape", "._../bad/../../escape",
                 "__MACOSX/../../escape", "safe\x00bad"):
        code, out, err = mock_sit([sit_entry(), sit_entry(name)], files={"safe.txt": "safe"})
        check(code != 0 and not out and not os.path.exists(j("called")),
              f"StuffIt validates every path before unar: {name!r}")
    for metadata in ({"XADIsLink": 1}, {"XADIsHardLink": 1},
                     {"XADLinkDestination": "../escape"}, {"XADIsEncrypted": 1},
                     {"XADIsCharacterDevice": 1}, {"XADIsBlockDevice": 1},
                     {"XADIsFIFO": 1}, {"XADIsSocket": 1},
                     {"XADPosixPermissions": stat.S_IFLNK | 0o777},
                     {"XADPosixPermissions": stat.S_IFIFO | 0o644},
                     {"XADDeviceMajor": 0}, {"XADIsCorrupted": 1}):
        code, out, err = mock_sit([sit_entry(), sit_entry("._metadata", **metadata)])
        check(code != 0 and not out and not os.path.exists(j("called")),
              f"StuffIt rejects metadata before unar: {metadata}")
    for options in ({"properties": {"XADIsEncrypted": 1}}, {"broken_json": True},
                    {"listing_exit": 1}):
        code, out, err = mock_sit([sit_entry()], **options)
        check(code != 0 and not out and not os.path.exists(j("called")),
              f"StuffIt rejects archive metadata/listing errors: {options}")
    for size in (-1, "5", 1 << 31):
        code, out, err = mock_sit([{"XADFileName": "big", "XADFileSize": size}])
        check(code != 0 and not out and not os.path.exists(j("called")),
              f"StuffIt refuses invalid/oversized lengths: {size}")
    code, out, err = mock_sit([sit_entry()] * 10001)
    check(code != 0 and not out and not os.path.exists(j("called")),
          "StuffIt preflight entry count is bounded")
    code, out, err = mock_sit([{"XADFileName": "large", "XADFileSize": 1 << 30}] * 3)
    check(code != 0 and not out and not os.path.exists(j("called")),
          "StuffIt preflight total size is bounded")
    code, out, err = mock_sit([])
    check(code != 0 and "empty" in err and not os.path.exists(j("called")),
          "empty StuffIt does not run unar")
    for special in ("symlink", "hardlink", "fifo"):
        code, out, err = mock_sit([sit_entry("first")], files={"first": "safe"}, special=special)
        check(code != 0 and not out and "link or special" in err,
              f"StuffIt verifies staging and refuses decoder-produced {special}")
    code, out, err = mock_sit([sit_entry()], files={"partial.txt": "partial"}, extract_exit=1)
    check(code != 0 and not out and "couldn't be expanded" in err,
          "failed StuffIt extraction publishes nothing")
    code, out, err = mock_sit([sit_entry("nested.zip"), sit_entry("._nested.zip")],
                             files={"nested.zip": "not expanded", "._nested.zip": "fork",
                                    ".DS_Store": "kept", "__MACOSX/metadata": "kept",
                                    "website:theme": "legal Mac filename"})
    check(code == 0 and out == j("Mock") and
          open(j("Mock", "._nested.zip")).read() == "fork" and
          open(j("Mock", "nested.zip")).read() == "not expanded" and
          os.path.exists(j("Mock", ".DS_Store")) and
          os.path.exists(j("Mock", "__MACOSX", "metadata")) and
          os.path.exists(j("Mock", "website:theme")),
          "StuffIt keeps fork sidecars and metadata; nested archives are not expanded")
    check(os.path.isfile(j("Mock.sit")), "StuffIt keeps the original archive")

    namespace = runpy.run_path(TOOL, run_name="expand_test")
    companion_root = j("companions")
    os.mkdir(companion_root)
    apple_double = (struct.pack(">II16sH", 0x00051607, 0x00020000, bytes(16), 1)
                    + struct.pack(">III", 2, 38, 4) + b"fork")
    for target in ("existing", "missing", "directory", "link", "hardlink", ".", ".."):
        sidecar = os.path.join(companion_root, "._" + target)
        with open(sidecar, "wb") as output:
            output.write(apple_double)
    with open(os.path.join(companion_root, "existing"), "wb") as output:
        output.write(b"keep existing data")
    os.mkdir(os.path.join(companion_root, "directory"))
    os.symlink("existing", os.path.join(companion_root, "link"))
    with open(os.path.join(companion_root, "hardlink-source"), "wb") as output:
        output.write(b"hardlink")
    os.link(os.path.join(companion_root, "hardlink-source"),
            os.path.join(companion_root, "hardlink"))
    namespace["sit_companions"](companion_root, [
        os.path.join(companion_root, "._existing"), os.path.join(companion_root, "._missing")])
    check(open(os.path.join(companion_root, "existing"), "rb").read() == b"keep existing data"
          and os.path.getsize(os.path.join(companion_root, "missing")) == 0,
          "AppleDouble companion creation never overwrites existing regular data")
    for target in ("directory", "link", "hardlink", ".", ".."):
        with patch("sys.stderr", new_callable=io.StringIO) as stderr:
            try:
                namespace["sit_companions"](companion_root,
                    [os.path.join(companion_root, "._" + target)])
                check(False, "unsafe AppleDouble target should exit")
            except SystemExit as exit:
                check(exit.code == 1 and "target" in stderr.getvalue(),
                      f"AppleDouble companion rejects unsafe/nonregular target {target!r}")
    with open(os.path.join(companion_root, "._broken"), "wb") as output:
        output.write(apple_double[:-1])
    with patch("sys.stderr", new_callable=io.StringIO) as stderr:
        try:
            namespace["sit_companions"](companion_root,
                [os.path.join(companion_root, "._broken")])
            check(False, "truncated AppleDouble should exit")
        except SystemExit as exit:
            check(exit.code == 1 and "invalid AppleDouble" in stderr.getvalue()
                  and not os.path.exists(os.path.join(companion_root, "broken")),
                  "AppleDouble companion requires valid resource entry bounds")
    for error, message in ((subprocess.TimeoutExpired("unar", 120), "too long"),
                           (FileNotFoundError(), "Install unar")):
        with patch("subprocess.run", side_effect=error), patch("sys.stderr", new_callable=io.StringIO) as stderr:
            try:
                namespace["sit_process"](["unar"], subprocess.DEVNULL, 1024)
                check(False, "StuffIt decoder failure should exit")
            except SystemExit as exit:
                check(exit.code == 1 and message in stderr.getvalue(),
                      f"StuffIt decoder errors are bounded and actionable: {message}")

    check(not [n for n in os.listdir(d) if n.startswith(".expanding-")],
          "no half-made folders are left behind")

print("FAILED" if fails else "all passed")
sys.exit(1 if fails else 0)

"""zacos9-expand: archives expanded in place (shell/finder/zacos9-expand).

    python3 tests/finder/test_expand.py shell/finder/zacos9-expand
"""
import io
import base64
import os
import subprocess
import sys
import tarfile
import tempfile
import zipfile
import libarchive

TOOL = os.path.abspath(sys.argv[1])
fails = 0


def check(ok, what):
    global fails
    print(("ok    " if ok else "FAIL  ") + what)
    fails += not ok


def expand(path):
    r = subprocess.run([sys.executable, TOOL, path], capture_output=True, text=True, timeout=30)
    return r.returncode, r.stdout.strip(), r.stderr.strip()


def seven_zip(path, entries):
    with libarchive.file_writer(path, "7zip") as archive:
        for name, data, mode in entries:
            archive.add_file_from_memory(name, len(data), data, permission=mode)


with tempfile.TemporaryDirectory() as d:
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

    check(not [n for n in os.listdir(d) if n.startswith(".expanding-")],
          "no half-made folders are left behind")

print("FAILED" if fails else "all passed")
sys.exit(1 if fails else 0)

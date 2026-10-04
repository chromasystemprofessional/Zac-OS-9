"""zacos9-expand: archives expanded in place (shell/finder/zacos9-expand).

    python3 tests/finder/test_expand.py shell/finder/zacos9-expand
"""
import io
import os
import subprocess
import sys
import tarfile
import tempfile
import zipfile

TOOL = os.path.abspath(sys.argv[1])
fails = 0


def check(ok, what):
    global fails
    print(("ok    " if ok else "FAIL  ") + what)
    fails += not ok


def expand(path):
    r = subprocess.run([sys.executable, TOOL, path], capture_output=True, text=True)
    return r.returncode, r.stdout.strip(), r.stderr.strip()


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

    check(not [n for n in os.listdir(d) if n.startswith(".expanding-")],
          "no half-made folders are left behind")

print("FAILED" if fails else "all passed")
sys.exit(1 if fails else 0)

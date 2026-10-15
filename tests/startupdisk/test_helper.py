"""zacos9-startupdisk-helper against a Debian-style grub.cfg: the entries
and their ids, the default (by number, by saved id, by title), "set" making
GRUB remember (GRUB_DEFAULT=saved, regenerated once), refusing ids it
doesn't list, and "detect" turning os-prober on.

usage: test_helper.py HELPER GRUB.CFG
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

helper, cfg = sys.argv[1], sys.argv[2]
failures = 0


def check(ok, what):
    global failures
    print(("ok    " if ok else "FAIL  ") + what)
    failures += not ok


with tempfile.TemporaryDirectory() as d:
    root = Path(d)
    defaults, grubenv, updated = root / "grub", root / "grubenv", root / "updated"
    defaults.write_text("GRUB_DEFAULT=0\n# GRUB_DISABLE_OS_PROBER=false\nGRUB_TIMEOUT=0\n")
    grubenv.write_text("# GRUB Environment Block\n")
    env = dict(os.environ, ZACOS9_GRUB_CFG=cfg, ZACOS9_GRUB_DEFAULTS=str(defaults),
               ZACOS9_GRUBENV=str(grubenv), ZACOS9_UPDATE_GRUB=f"touch {updated}",
               ZACOS9_GRUB_SET_DEFAULT=f"sh -c 'printf \"saved_entry=%s\\n\" \"$0\" > {grubenv}'")

    def run(*args):
        r = subprocess.run([helper, *args], env=env, capture_output=True, text=True, timeout=20)
        return r.returncode, r.stdout, r.stderr.strip()

    code, out, _ = run("list")
    lines = [l.split("\t") for l in out.splitlines()]
    entries = [l[1:] for l in lines if l[0] == "entry"]
    check(code == 0 and lines[0] == ["default", "gnulinux-simple-1234-abcd"],
          "GRUB_DEFAULT=0: the first entry is the default")
    check([e[0] for e in entries] == [
        "gnulinux-simple-1234-abcd",
        "gnulinux-advanced-1234-abcd>gnulinux-6.12.111+deb13-amd64-advanced-1234-abcd",
        "gnulinux-advanced-1234-abcd>gnulinux-6.12.111+deb13-amd64-recovery-1234-abcd",
        "osprober-efi-ABCD-1234", "custom-joe", "uefi-firmware"],
        "every entry, a submenu's as submenu>entry ids, functions and if-blocks skipped")
    check([e[2] for e in entries] == ["linux", "linux", "linux", "windows", "other", "firmware"] and
          [e[3] for e in entries] == ["0", "1", "1", "0", "0", "0"], "kinds and depths")
    check(entries[4][1] == "Joe's \"Test\" System", "titles unquoted as GRUB's shell does ('\\'' and \"\")")

    code, _, err = run("set", "nonsense")
    check(code == 1 and "isn't one of" in err and not updated.exists(), "an id it doesn't list is refused")

    code, _, _ = run("set", "osprober-efi-ABCD-1234")
    check(code == 0 and "GRUB_DEFAULT=saved" in defaults.read_text() and updated.exists(),
          "the first choice makes GRUB remember one (GRUB_DEFAULT=saved) and regenerates grub.cfg")
    check(run("list")[1].splitlines()[0] == "default\tosprober-efi-ABCD-1234", "and it is the default")
    updated.unlink()
    code, _, _ = run("set", "gnulinux-advanced-1234-abcd>gnulinux-6.12.111+deb13-amd64-advanced-1234-abcd")
    check(code == 0 and not updated.exists() and
          run("list")[1].splitlines()[0].endswith("advanced-1234-abcd"),
          "after that, only grub-set-default: a submenu entry too")
    grubenv.write_text("saved_entry=Windows Boot Manager (on /dev/nvme0n1p1)\n")
    check(run("list")[1].splitlines()[0] == "default\tosprober-efi-ABCD-1234",
          "a default saved by title is found by its title")

    code, _, _ = run("detect")
    text = defaults.read_text()
    check(code == 0 and "\nGRUB_DISABLE_OS_PROBER=false" in "\n" + text and
          "# GRUB_DISABLE_OS_PROBER=false" in text and updated.exists(),
          "Look for Other Systems turns os-prober on (the comment left alone) and regenerates")

    env["ZACOS9_GRUB_CFG"] = str(root / "missing.cfg")
    code, _, err = run("list")
    check(code == 1 and "GRUB" in err, "no grub.cfg: it says the computer doesn't start up through GRUB")

sys.exit(1 if failures else 0)

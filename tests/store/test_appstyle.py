"""Tests for appstore/zacos9-appstyle with a made-up preset (a zip built here,
fetched through a file:// URL), in a private HOME.

    python3 tests/store/test_appstyle.py appstore/zacos9-appstyle
"""
import hashlib
import importlib.machinery
import importlib.util
import os
import re
import sys
import tempfile
import zipfile

root = tempfile.mkdtemp(prefix="appstyle-test-")
os.environ.update(HOME=root, XDG_DATA_HOME=root + "/data", XDG_CACHE_HOME=root + "/cache")

loader = importlib.machinery.SourceFileLoader("appstyle", sys.argv[1])
spec = importlib.util.spec_from_loader("appstyle", loader)
appstyle = importlib.util.module_from_spec(spec)
loader.exec_module(appstyle)

fails = 0


def check(ok, what):
    global fails
    print(("ok    " if ok else "FAIL  ") + what)
    fails += not ok


def make_zip(path, extra=()):
    prefix = "Preset/.config/App/1.0/"
    with zipfile.ZipFile(path, "w") as z:
        z.writestr(prefix + "shortcutsrc", "photoshop shortcuts\n")
        z.writestr(prefix + "toolrc", "photoshop tools\n")
        z.writestr(prefix + "gimprc", "(undo-levels 8)\n(monitor-xresolution 140)\n(monitor-yresolution 140)\n(thumbnail-size large)\n")
        z.writestr(prefix + "tool-options/paintbrush", "brush\n")
        z.writestr(prefix + "sessionrc",
                   '(session-info "toplevel"\n    (factory-entry "main")\n    (position 16 16)\n'
                   '    (size 1888 1011)\n    (monitor 1)\n    (gimp-dock\n        (book\n'
                   '            (position 741)\n            (current-page 0))))\n'
                   '(session-info "toplevel"\n    (factory-entry "dialog")\n    (position 2559 856))\n'
                   '(single-window-mode yes)\n')
        z.writestr(prefix + "splashes/splash.png", "artwork\n")
        z.writestr(prefix + "../escape.txt", "must never land outside\n")
        z.writestr("Preset/.local/share/icons/photo.png", "not ours\n")
        for name, data in extra:
            z.writestr(prefix + name, data)


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def register(zip_path, sha256=None, target=None):
    appstyle.STYLES["test"] = {
        "name": "Test", "program": r"zacos9-appstyle-test-never-running", "program_label": "Test App",
        "url": "file://" + zip_path, "sha256": sha256 or sha(zip_path),
        "member_prefix": "Preset/.config/App/1.0/",
        "target": target or root + "/config/App/1.0",
        "skip": ("splashes/",),
        "edits": {
            "gimprc": [(re.compile(r"^\(monitor-[xy]resolution [^\n]*\n", re.M), "")],
            "sessionrc": [(re.compile(r"[ \t]*\((?:position|size|monitor)(?:[ \t]+-?\d+)+\)"), "")],
        },
    }
    return appstyle.STYLES["test"]


def tree(d):
    out = {}
    for base, _, files in os.walk(d):
        for f in files:
            p = os.path.join(base, f)
            out[os.path.relpath(p, d)] = open(p).read()
    return out


def fails_with(fn, *args):
    try:
        fn(*args)
    except appstyle.Failure as e:
        return str(e)
    return None


zip1 = root + "/preset.zip"
make_zip(zip1)
style = register(zip1)
target = style["target"]
os.makedirs(target)
open(target + "/shortcutsrc", "w").write("my own shortcuts\n")
open(target + "/keepme", "w").write("untouched\n")
before = tree(target)

check(not appstyle.is_on("test"), "off to begin with")
appstyle.apply("test")
after = tree(target)
check(appstyle.is_on("test"), "on after apply")
check(after["shortcutsrc"] == "photoshop shortcuts\n", "a settings file is replaced")
check(after["toolrc"] == "photoshop tools\n" and after["tool-options/paintbrush"] == "brush\n",
      "new files, in folders, are added")
check("monitor" not in after["gimprc"] and "undo-levels" in after["gimprc"] and "thumbnail-size" in after["gimprc"],
      "the author's monitor resolution is dropped, the rest of gimprc kept")
check("splashes/splash.png" not in after, "the splash screen (artwork) is left out")
s = after["sessionrc"]
check(not re.search(r"\((position|size|monitor)\b", s), "window positions, sizes and monitors are cut out of sessionrc")
check("gimp-dock" in s and "current-page 0" in s and "single-window-mode yes" in s,
      "and the layout is kept")
check(s.count("(") == s.count(")") and s.rstrip().endswith("(single-window-mode yes)"),
      "with its brackets still balanced")
check(not any(".local" in k or "icons" in k for k in after), "files outside the settings folder are left out")
check(not os.path.exists(root + "/config/App/escape.txt") and not os.path.exists(root + "/config/escape.txt"),
      "a '..' path in the zip is refused")
check(after["keepme"] == "untouched\n", "files the preset doesn't mention are left alone")

appstyle.apply("test")
check(tree(target) == after, "applying twice changes nothing")

appstyle.reset("test")
check(tree(target) == before, "reset puts everything back exactly (replaced files restored, added ones gone)")
check(not os.path.exists(target + "/tool-options"), "and removes folders the preset made")
check(not appstyle.is_on("test") and not os.path.exists(appstyle.backup_dir("test")), "and forgets it was on")

# The wrong download.
bad = register(zip1, sha256="0" * 64)
os.unlink(appstyle.cache_dir() + "/" + sha(zip1) + ".zip")
msg = fails_with(appstyle.apply, "test")
check(msg is not None and "nothing was changed" in msg and tree(target) == before and not appstyle.is_on("test"),
      "a download that doesn't match its checksum changes nothing")

# The application is running.
style = register(zip1)
style["program"] = r"python3?"  # this test is a running python
msg = fails_with(appstyle.apply, "test")
check(msg is not None and "Quit Test App first" in msg and tree(target) == before,
      "a running application is not touched")
style["program"] = r"zacos9-appstyle-test-never-running"

# A failure part way through is rolled back.
zip2 = root + "/preset2.zip"
make_zip(zip2, extra=[("toolrc/inside", "a folder where a file is expected\n")])
register(zip2)
try:
    appstyle.apply("test")
    rolled = False
except OSError:
    rolled = True
check(rolled and tree(target) == before and not appstyle.is_on("test"),
      "a failure part way through puts back what was already replaced")

print("all passed" if not fails else "%d failed" % fails)
sys.exit(1 if fails else 0)

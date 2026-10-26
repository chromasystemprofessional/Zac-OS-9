"""The fonts programs see in a ZacOS session (share/fontconfig/zacos9-fonts.conf):
its families are the Fonts folder's (fontMacFamilies() in shell/files/fonts.cpp);
through it fontconfig lists only those and the user's own fonts, which
zacos9/fonts-own.conf accepts by file; Helvetica, Arial and serif find their
stand-ins. Runs fc-list and fc-match against this computer's fonts.

usage: test_fonts_for_programs.py CONF FONTS_CPP
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

conf, fonts_cpp = Path(sys.argv[1]).resolve(), Path(sys.argv[2])
failures = 0


def check(ok, what):
    global failures
    print(("ok    " if ok else "FAIL  ") + what)
    failures += not ok


source = fonts_cpp.read_text()
body = source[source.index("const QStringList &fontMacFamilies()"):]
body = body[:body.index("};")]
families = re.findall(r'"([^"]+)"', body)
accepted = re.findall(r'<patelt name="family"><string>([^<]+)</string>', conf.read_text())
check(families and accepted == families, "the rule accepts the Fonts folder's families, the same list")


def fc(home, *args):
    env = {k: v for k, v in os.environ.items() if not k.startswith("XDG_")}
    env.update(FONTCONFIG_FILE=str(conf), HOME=str(home), XDG_CACHE_HOME=str(home / ".cache"),
               XDG_CONFIG_HOME=str(home / ".config"), XDG_DATA_HOME=str(home / ".local/share"))
    return subprocess.run(args, env=env, capture_output=True, text=True, timeout=120).stdout


def listed(home):
    """Each font's family: the first of its names (DejaVu's Condensed faces
    are also called "DejaVu Sans Condensed")."""
    return {line.split(",")[0] for line in fc(home, "fc-list", ":", "family").splitlines() if line}


with tempfile.TemporaryDirectory() as d:
    home = Path(d)
    seen = listed(home)
    check(bool(seen) and seen <= set(families), f"programs see only those families ({len(seen)} installed here)")
    if "Nimbus Sans" in seen:
        check('"Nimbus Sans"' in fc(home, "fc-match", "Helvetica"), "Helvetica finds Nimbus Sans")
    if "Nimbus Roman" in seen:
        check('"Nimbus Roman"' in fc(home, "fc-match", "serif"), "serif finds Nimbus Roman (Times)")
    if "Liberation Sans" in seen:
        check('"Liberation Sans"' in fc(home, "fc-match", "Arial"), "Arial finds Liberation Sans")

    # A font of the user's own: a copy of one the rule hides.
    everything = subprocess.run(["fc-list", "--format", "%{file}|%{family}\n"], capture_output=True,
                                text=True, timeout=120).stdout.splitlines()
    other = None
    for line in everything:
        file, _, fam = line.partition("|")
        if file.endswith((".ttf", ".otf")) and fam and not set(fam.split(",")) & set(families):
            other = (file, fam)
            break
    if other is None:
        print("skip  no other font installed to stand in for the user's own")
    else:
        own = home / ".local/share/fonts" / ("Mine" + Path(other[0]).suffix)
        own.parent.mkdir(parents=True)
        shutil.copy(other[0], own)
        name = other[1].split(",")[0]
        check(name not in listed(home), "the user's own font is hidden until the Sniffer accepts it")
        accepts = home / ".config/zacos9/fonts-own.conf"
        accepts.parent.mkdir(parents=True)
        accepts.write_text('<?xml version="1.0"?>\n<fontconfig><selectfont><acceptfont><pattern>'
                           f'<patelt name="file"><string>{own}</string></patelt></pattern></acceptfont>'
                           '</selectfont></fontconfig>\n')
        check(name in listed(home), "accepted by file in fonts-own.conf, programs see it")

sys.exit(1 if failures else 0)

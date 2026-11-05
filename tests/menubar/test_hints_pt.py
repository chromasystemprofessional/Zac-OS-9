#!/usr/bin/env python3
"""The menu bar's labels and Hint Bubbles stay paired in Brazilian Portuguese.

hints.c finds a hint by the item's label, so a translated label that has no
translated label in the hints table would silently lose its bubble. Compiles
po/pt.po, runs the test binary's `pt` mode, and checks it found one.

    test_hints_pt.py <source root> <test-hints binary>
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(sys.argv[1])
BINARY = Path(sys.argv[2])


def run(cmd, **kwargs):
    result = subprocess.run(cmd, capture_output=True, text=True, **kwargs)
    if result.returncode != 0:
        sys.exit(f"{' '.join(map(str, cmd))} failed:\n{result.stdout}{result.stderr}")
    return result


def main():
    for tool in ("msgfmt", "localedef"):
        if not shutil.which(tool):
            sys.exit(f"test_hints_pt: {tool} is missing")
    with tempfile.TemporaryDirectory(prefix="zacos9-hints-pt-") as tmp:
        tmp = Path(tmp)
        mo = tmp / "locale" / "pt" / "LC_MESSAGES" / "zacos9.mo"
        mo.parent.mkdir(parents=True)
        run(["msgfmt", "-o", str(mo), str(ROOT / "po" / "pt.po")])
        locales = tmp / "locales"
        locales.mkdir()
        run(["localedef", "-i", "en_US", "-f", "UTF-8", str(locales / "en_US.UTF-8")])
        env = dict(os.environ, LC_ALL="en_US.UTF-8", LANG="en_US.UTF-8",
                   LANGUAGE="pt", LOCPATH=str(locales), ZACOS9_LOCALEDIR=str(tmp / "locale"))
        out = run([str(BINARY), "pt"], env=env)
        if "Lixo" not in out.stdout:
            sys.exit(f"test_hints_pt: got {out.stdout!r}")


if __name__ == "__main__":
    main()

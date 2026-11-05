#!/usr/bin/env python3
"""The menu bar's labels and Hint Bubbles stay paired in Italian.

hints.c finds a hint by the item's label, so a translated label that has no
translated label in the hints table would silently lose its bubble. Compiles
po/it.po, runs the test binary's `it` mode, and checks it found one.

    test_hints_it.py <source root> <test-hints binary>
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
            sys.exit(f"test_hints_it: {tool} is missing")
    with tempfile.TemporaryDirectory(prefix="zacos9-hints-it-") as tmp:
        tmp = Path(tmp)
        mo = tmp / "locale" / "it" / "LC_MESSAGES" / "zacos9.mo"
        mo.parent.mkdir(parents=True)
        run(["msgfmt", "-o", str(mo), str(ROOT / "po" / "it.po")])
        locales = tmp / "locales"
        locales.mkdir()
        run(["localedef", "-i", "en_US", "-f", "UTF-8", str(locales / "en_US.UTF-8")])
        env = dict(os.environ, LC_ALL="en_US.UTF-8", LANG="en_US.UTF-8",
                   LANGUAGE="it", LOCPATH=str(locales), ZACOS9_LOCALEDIR=str(tmp / "locale"))
        out = run([str(BINARY), "it"], env=env)
        if "Cestino" not in out.stdout:
            sys.exit(f"test_hints_it: got {out.stdout!r}")


if __name__ == "__main__":
    main()

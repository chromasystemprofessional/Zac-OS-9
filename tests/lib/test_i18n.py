#!/usr/bin/env python3
"""The gettext pipeline, end to end (docs/i18n.md).

Builds po/zacos9.pot from the files in po/POTFILES.in, turns it into the
pseudo-locale, compiles it, and runs the C API under it with LANGUAGE=qqq.
Then checks that ZACOS9_TEXT_STRICT names a string that no longer fits.

Run by meson as the `i18n` test:

    test_i18n.py <source root> <test-i18n binary>
"""
import importlib.util
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


def load_pseudo():
    path = ROOT / "tools" / "i18n" / "pseudo.py"
    spec = importlib.util.spec_from_file_location("zacos9_pseudo", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    for tool in ("xgettext", "msgfmt", "localedef"):
        if not shutil.which(tool):
            sys.exit(f"test_i18n: {tool} is missing (the gettext and locales packages)")

    potfiles = [line.strip() for line in (ROOT / "po" / "POTFILES.in").read_text().splitlines()
                if line.strip() and not line.startswith("#")]
    if not potfiles:
        sys.exit("test_i18n: po/POTFILES.in lists no files")

    pseudo = load_pseudo()
    with tempfile.TemporaryDirectory(prefix="zacos9-i18n-") as tmp:
        tmp = Path(tmp)
        pot = tmp / "zacos9.pot"
        run(["xgettext", "--from-code=UTF-8", "--keyword=_", "--keyword=N_",
             "--keyword=tr9", "-o", str(pot), *potfiles], cwd=ROOT)

        qqq = tmp / "qqq.po"
        qqq.write_text(pseudo.pseudo_po(pot.read_text(encoding="utf-8")), encoding="utf-8")

        mo = tmp / "locale" / "qqq" / "LC_MESSAGES" / "zacos9.mo"
        mo.parent.mkdir(parents=True)
        run(["msgfmt", "-o", str(mo), str(qqq)])

        # LANGUAGE is only honoured for a real locale, not C: make one here,
        # in a private LOCPATH, rather than depend on the test machine's.
        locales = tmp / "locales"
        locales.mkdir()
        run(["localedef", "-i", "en_US", "-f", "UTF-8", str(locales / "en_US.UTF-8")])

        env = dict(os.environ, LC_ALL="en_US.UTF-8", LANG="en_US.UTF-8",
                   LANGUAGE="qqq", LOCPATH=str(locales),
                   ZACOS9_LOCALEDIR=str(tmp / "locale"))
        result = run([str(BINARY)], env=env)
        expected = pseudo.pseudo_text("Mouse") + "\n" + pseudo.pseudo_text("Double-Click Speed") + "\n"
        if result.stdout != expected:
            sys.exit(f"test_i18n: got {result.stdout!r}, expected {expected!r}")

        result = run([str(BINARY), "strict"])
        if "text truncated to 20 px" not in result.stderr:
            sys.exit(f"test_i18n: no truncation warning for a long string:\n{result.stderr}")

        # The real catalogs, installed the way the package installs them.
        italian = tmp / "locale-it" / "it" / "LC_MESSAGES" / "zacos9.mo"
        italian.parent.mkdir(parents=True)
        run(["msgfmt", "--check", "-o", str(italian), str(ROOT / "po" / "it.po")])
        env["LANGUAGE"] = "it"
        env["ZACOS9_LOCALEDIR"] = str(tmp / "locale-it")
        result = run([str(BINARY)], env=env)
        lines = result.stdout.splitlines()
        if len(lines) != 2 or lines[1] == "Double-Click Speed" or "Velocità" not in lines[1]:
            sys.exit(f"test_i18n: Italian catalog gave {result.stdout!r}")

        # Japanese is a pilot: the Mouse panel's strings, with the rest
        # falling back to English one string at a time.
        japanese = tmp / "locale-ja" / "ja" / "LC_MESSAGES" / "zacos9.mo"
        japanese.parent.mkdir(parents=True)
        run(["msgfmt", "--check", "-o", str(japanese), str(ROOT / "po" / "ja.po")])
        env["LANGUAGE"] = "ja"
        env["ZACOS9_LOCALEDIR"] = str(tmp / "locale-ja")
        result = run([str(BINARY)], env=env)
        if result.stdout.splitlines() != ["マウス", "ダブルクリックの速さ"]:
            sys.exit(f"test_i18n: Japanese catalog gave {result.stdout!r}")

        # Russian: the same first slice, in Cyrillic.
        russian = tmp / "locale-ru" / "ru" / "LC_MESSAGES" / "zacos9.mo"
        russian.parent.mkdir(parents=True)
        run(["msgfmt", "--check", "-o", str(russian), str(ROOT / "po" / "ru.po")])
        env["LANGUAGE"] = "ru"
        env["ZACOS9_LOCALEDIR"] = str(tmp / "locale-ru")
        result = run([str(BINARY)], env=env)
        if result.stdout.splitlines() != ["Мышь", "Скорость двойного щелчка"]:
            sys.exit(f"test_i18n: Russian catalog gave {result.stdout!r}")


if __name__ == "__main__":
    main()

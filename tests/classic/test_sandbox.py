"""zacos9-classic: the guest cannot read the rest of the home.

The launcher runs the emulator in bubblewrap. This test puts a fake
``SheepShaver`` in the sandbox: it fails if ~/.ssh or a desktop setting is
readable, and writes to the Classic folder and its private HOME. Skips when
bubblewrap or user namespaces are unavailable (for example inside some CI
containers), because the sandbox then cannot be exercised.

usage: test_sandbox.py LAUNCHER
"""
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

launcher = sys.argv[1]
failures = 0


def check(ok, what):
    global failures
    print(("ok    " if ok else "FAIL  ") + what)
    failures += not ok


def bubblewrap_works():
    if not shutil.which("bwrap"):
        return False
    try:
        result = subprocess.run(
            ["bwrap", "--unshare-all", "--ro-bind", "/", "/", "--", "/bin/true"],
            capture_output=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        return False
    return result.returncode == 0


if not bubblewrap_works():
    print("skip: bubblewrap or unprivileged user namespaces are unavailable")
    sys.exit(0)

with tempfile.TemporaryDirectory(dir=Path.home()) as d:
    root = Path(d)
    home = root / "home"
    classic = home / "Classic"
    emulators = root / "emulators"
    runtime = root / "run"
    for folder in (classic, emulators, runtime, home / ".ssh", home / ".config" / "zacos9"):
        folder.mkdir(parents=True, exist_ok=True)
    (home / ".ssh" / "id_rsa").write_text("a private key\n")
    (home / ".config" / "zacos9" / "desktop.conf").write_text("a setting=secret\n")
    (classic / "rom").write_bytes(bytes.fromhex("9630c68b") + bytes(4 * 1024 * 1024 - 4))

    sheep = emulators / "SheepShaver"
    sheep.write_text(
        "#!/bin/sh\n"
        '[ -r "$HOST_HOME/.ssh/id_rsa" ] && exit 42\n'
        '[ -e "$HOST_HOME/.config/zacos9/desktop.conf" ] && exit 43\n'
        '[ "$HOME" = "$HOST_HOME/.config/zacos9/classic-home" ] || exit 44\n'
        'printf ok > "$HOST_HOME/Classic/sandbox-wrote" || exit 45\n'
        'printf prefs > "$HOME/prefs-ok" || exit 46\n'
        "exit 0\n")
    sheep.chmod(0o755)

    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("XDG_", "ZACOS9_"))}
    env.update(
        HOME=str(home),
        HOST_HOME=str(home),
        ZACOS9_CLASSIC_DIR=str(classic),
        ZACOS9_EMULATORS=str(emulators),
        ZACOS9_CLASSIC_SANDBOX="yes",
        ZACOS9_CLASSIC_WINDOWS="0",
        XDG_RUNTIME_DIR=str(runtime),
    )
    result = subprocess.run([launcher], env=env, capture_output=True, text=True,
                            timeout=30)
    check(result.returncode == 0,
          "the sandboxed emulator starts" +
          (f" ({result.stderr.strip()})" if result.returncode else ""))
    check_result = subprocess.run([launcher, "--check"], env=env,
                                  capture_output=True, text=True, timeout=30)
    check(check_result.returncode == 0 and
          "sandbox: bwrap (network: no, gpu: no)" in check_result.stdout,
          "--check reports the sandbox and its defaults")
    check((classic / "sandbox-wrote").is_file(),
          "the Classic folder is writable inside the sandbox")
    check((home / ".config" / "zacos9" / "classic-home" / "prefs-ok").is_file(),
          "the emulator has a private, writable HOME")
    check((home / ".ssh" / "id_rsa").is_file(),
          "the test's key is still on the host")
    check("42" not in result.stderr and "43" not in result.stderr,
          "the guest could not read ~/.ssh or the desktop settings")

sys.exit(1 if failures else 0)

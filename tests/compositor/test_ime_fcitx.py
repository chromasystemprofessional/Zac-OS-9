#!/usr/bin/env python3
"""Smoke test with the real Fcitx 5 as the input method: it should
connect to the compositor's input-method global, activate for the
test program's field, and a letter typed on the virtual keyboard
should come back from Fcitx's own virtual keyboard into the program
(the key pass-through path), proving the grab loop works both ways.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time

WM, APP, VKBD = sys.argv[1], sys.argv[2], sys.argv[3]

if not shutil.which("fcitx5") or not shutil.which("dbus-run-session"):
    print("skip: fcitx5 or dbus-run-session is not installed")
    sys.exit(77)


def wait_for(path, needle, timeout=25.0):
    end = time.time() + timeout
    while time.time() < end:
        try:
            with open(path, errors="replace") as f:
                if needle in f.read():
                    return True
        except FileNotFoundError:
            pass
        time.sleep(0.2)
    return False


def main():
    results = []

    def check(name, ok):
        results.append((name, ok))
        print(("ok  " if ok else "FAIL") + " - " + name)

    runtime = tempfile.mkdtemp(prefix="zacos-fcitx-")
    os.chmod(runtime, 0o700)
    env = dict(os.environ)
    env.update({
        "XDG_RUNTIME_DIR": runtime,
        "WLR_BACKENDS": "headless",
        "WLR_RENDERER": "pixman",
        "WLR_HEADLESS_OUTPUTS": "1",
        "LIBINPUT_NO_DEVICES": "1",
        "XDG_CONFIG_HOME": os.path.join(runtime, "config"),
        "HOME": runtime,
        "LANG": "C.UTF-8",
    })
    procs = []
    logs = {}
    try:
        for name, cmd in (("wm", [WM]), ("app", [APP])):
            logs[name] = os.path.join(runtime, name + ".log")
            f = open(logs[name], "w")
            procs.append(subprocess.Popen(cmd, env=env, stdout=f, stderr=subprocess.STDOUT))
            if name == "wm":
                end = time.time() + 20
                sock = None
                while time.time() < end:
                    for p in os.listdir(runtime):
                        if p.startswith("wayland-") and not p.endswith(".lock"):
                            sock = p
                            break
                    if sock:
                        break
                    time.sleep(0.2)
                env["WAYLAND_DISPLAY"] = sock
            else:
                wait_for(logs["app"], "APP keyboard-enter", timeout=10)
            time.sleep(0.5)

        # Baseline: before fcitx, a typed key goes straight in.
        subprocess.run([VKBD, "30"], env=env, timeout=30,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        wait_for(logs["app"], "APP key keycode=30 state=1")

        logs["fcitx"] = os.path.join(runtime, "fcitx.log")
        f = open(logs["fcitx"], "w")
        procs.append(subprocess.Popen(
            ["dbus-run-session", "--", "fcitx5"], env=env,
            stdout=f, stderr=subprocess.STDOUT))
        check("fcitx starts (its wayland input method loads)",
              wait_for(logs["fcitx"], "Loaded addon waylandim", timeout=30))
        # Let it finish starting and take the grab before typing.
        wait_for(logs["fcitx"], "org.fcitx.Fcitx5", timeout=30)
        time.sleep(5)

        app_before = open(logs["app"], errors="replace").read()
        subprocess.run([VKBD, "31"], env=env, timeout=30,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        ok_key = wait_for(logs["app"], "APP key keycode=31 state=1")
        app_after = open(logs["app"], errors="replace").read()
        # Through fcitx the key comes back on fcitx's own virtual
        # keyboard: the seat's keyboard switches to it first, which
        # the program sees as a fresh keyboard-enter. A key that went
        # straight in (no grab) adds no enter.
        check("typed key comes back into the program through fcitx",
              ok_key and app_after.count("APP keyboard-enter") >
              app_before.count("APP keyboard-enter"))
        check("fcitx still running", procs[-1].poll() is None)
    finally:
        for name, path in logs.items():
            print(f"--- {name} log ---")
            try:
                print(open(path, errors="replace").read()[-3000:])
            except FileNotFoundError:
                print("(none)")
        for p in procs:
            p.terminate()
        for p in procs:
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()
    failed = [n for n, ok in results if not ok]
    print(f"{len(results) - len(failed)}/{len(results)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

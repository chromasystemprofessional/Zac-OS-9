#!/usr/bin/env python3
"""Headless input-method test for zacos9-wm.

Starts the compositor (headless), a test program with a text field, a
stand-in input method and a virtual keyboard, and checks the relay:

  - the input method is told to activate when the field is in use, and
    hears the field's surrounding text
  - its preedit string reaches the program
  - its candidate popup is told the text cursor's rectangle
  - a key typed on the keyboard reaches the input method through the
    grab instead of the program, and the string it commits in answer
    reaches the program

Needs the three test clients built next to this file's sources:
app-test, ime-test, vkbd-test (see build in the chroot script).
"""
import os
import subprocess
import sys
import tempfile
import time

WM = sys.argv[1]
APP = sys.argv[2]
IME = sys.argv[3]
VKBD = sys.argv[4]


def wait_for(path, needle, timeout=20.0):
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

    runtime = tempfile.mkdtemp(prefix="zacos-ime-")
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
    })
    procs = []
    logs = {}
    try:
        for name, cmd in (("wm", [WM]), ("app", [APP]), ("ime", [IME])):
            logs[name] = os.path.join(runtime, name + ".log")
            f = open(logs[name], "w")
            procs.append(subprocess.Popen(cmd, env=env, stdout=f, stderr=subprocess.STDOUT))
            if name == "app":
                wait_for(logs["app"], "APP keyboard-enter", timeout=10)
            if name == "wm":
                # wait for the socket
                end = time.time() + 20
                sock = None
                while time.time() < end:
                    cands = [p for p in os.listdir(runtime) if p.startswith("wayland-")]
                    locks = [p for p in cands if p.endswith(".lock")]
                    if cands and not locks:
                        sock = os.path.join(runtime, cands[0])
                        break
                    if locks:
                        sock = os.path.join(runtime, locks[0][:-5])
                        break
                    time.sleep(0.2)
                if not sock:
                    check("compositor starts", False)
                    return finish(results)
                env["WAYLAND_DISPLAY"] = os.path.basename(sock)
            time.sleep(1.0)

        check("input method activates for the field",
              wait_for(logs["ime"], "IME activate"))
        check("input method hears the surrounding text",
              wait_for(logs["ime"], "IME surrounding text=hello"))
        check("preedit reaches the program",
              wait_for(logs["app"], "APP preedit text=あい"))
        check("popup is told the cursor rectangle",
              wait_for(logs["ime"], "IME popup rect x=12 y=20 w=2 h=14"))

        subprocess.run([VKBD], env=env, timeout=30,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        check("typed key reaches the input method through the grab",
              wait_for(logs["ime"], "IME grab key keycode=30 state=1"))
        check("the key does not reach the program directly",
              not wait_for(logs["app"], "APP key keycode=30", timeout=1.5))
        check("committed string reaches the program",
              wait_for(logs["app"], "APP commit text=愛"))
    finally:
        for name, path in logs.items():
            print(f"--- {name} log ---")
            try:
                print(open(path, errors="replace").read())
            except FileNotFoundError:
                print("(none)")
        for p in procs:
            p.terminate()
        for p in procs:
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()
    return finish(results)


def finish(results):
    failed = [n for n, ok in results if not ok]
    print(f"{len(results) - len(failed)}/{len(results)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Exercise the actual portal process on a private session bus."""

import os
import subprocess
import sys
import tempfile
import time


def call(*arguments):
    return subprocess.run(
        ["gdbus", "call", "--session", "--dest",
         "org.freedesktop.impl.portal.desktop.zacos9", *arguments],
        capture_output=True, text=True, timeout=3,
    )


def main():
    # Private Home and runtime folders: the backend rebuilds its startup-disk
    # tree there. Real application directories keep the warm-up slow enough
    # that the requests below arrive while it is still scanning.
    scratch = tempfile.TemporaryDirectory()
    runtime = os.path.join(scratch.name, "runtime")
    os.mkdir(runtime, 0o700)
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", HOME=scratch.name,
               XDG_DATA_HOME=os.path.join(scratch.name, "data"),
               XDG_CONFIG_HOME=os.path.join(scratch.name, "config"),
               XDG_RUNTIME_DIR=runtime)
    backend = subprocess.Popen([sys.argv[1]], env=env)
    try:
        for _ in range(400):
            if backend.poll() is not None:
                raise AssertionError("portal exited before registration")
            # Ask the bus instead of the portal: a call to the portal's name
            # would auto-start the installed backend in place of this build.
            owner = subprocess.run(
                ["gdbus", "call", "--session", "--dest", "org.freedesktop.DBus",
                 "--object-path", "/org/freedesktop/DBus", "--method",
                 "org.freedesktop.DBus.NameHasOwner",
                 "org.freedesktop.impl.portal.desktop.zacos9"],
                capture_output=True, text=True, timeout=3)
            if "true" in owner.stdout:
                break
            time.sleep(0.05)
        else:
            raise AssertionError("portal did not register")

        # The first requests are closed while the application scan is still
        # running; the last one waits for it so a displayed chooser is closed.
        for index in range(4):
            if index == 3:
                time.sleep(12)
            handle = f"/org/freedesktop/portal/desktop/request/lifecycle/r{index}"
            request = subprocess.Popen([
                "gdbus", "call", "--session", "--dest",
                "org.freedesktop.impl.portal.desktop.zacos9",
                "--object-path", "/org/freedesktop/portal/desktop",
                "--method", "org.freedesktop.impl.portal.FileChooser.OpenFile",
                handle, "com.visualstudio.code", "", "Lifecycle chooser", "{}",
            ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                for _ in range(100):
                    if request.poll() is not None:
                        stdout, stderr = request.communicate()
                        raise AssertionError(f"request failed before cancellation: {stdout} {stderr}")
                    close = call("--object-path", handle,
                                 "--method", "org.freedesktop.impl.portal.Request.Close")
                    if close.returncode == 0:
                        break
                    time.sleep(0.02)
                else:
                    raise AssertionError("request did not expose Close")
                stdout, stderr = request.communicate(timeout=5)
                if request.returncode or "uint32 1" not in stdout:
                    raise AssertionError(f"expected cancellation response: {stdout} {stderr}")
            finally:
                if request.poll() is None:
                    request.terminate()
                    request.wait(timeout=5)
            time.sleep(0.05)
            if backend.poll() is not None:
                raise AssertionError("closing the chooser terminated the portal process")
        print("Four actual backend requests cancelled; portal process remains available")
    finally:
        if backend.poll() is None:
            backend.terminate()
        backend.wait(timeout=5)


if __name__ == "__main__":
    main()

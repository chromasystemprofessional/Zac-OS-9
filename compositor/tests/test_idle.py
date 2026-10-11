#!/usr/bin/python3
"""Screen saver, display sleep and system sleep in zacos9-wm (src/idle.c),
on two headless screens with "minutes" shortened to 400 ms: both screens
show the screen saver (zacos9-screensaver, covering them: screenshots
with grim), then sleep, then the system sleeps; input wakes them and the
desktop shows again; a window filling one screen keeps that one awake
while the other sleeps, and keeps the system awake until it closes."""
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

compositor, client, vptr = (str(Path(arg).resolve()) for arg in sys.argv[1:4])
MINUTE = 400


def wait_for(what, check, seconds=5):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(0.02)
    raise AssertionError(f"timed out waiting for {what}")


with tempfile.TemporaryDirectory(prefix="zacos9-idle-") as directory:
    root = Path(directory)
    (root / "zacos9").mkdir()
    (root / "zacos9" / "desktop.conf").write_text(
        "[General]\nscreensaver=blank\nscreensaver-after=1\ndisplay-sleep=4\nsystem-sleep=6\n")
    slept = root / "slept"
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_CONFIG_HOME=directory,
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="2",
               ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="", ZACOS9_COLLAR="",
               ZACOS9_IDLE_MINUTE_MS=str(MINUTE),
               ZACOS9_SUSPEND_COMMAND=f"echo slept >>'{slept}'",
               QT_QPA_PLATFORM="wayland", QT_STYLE_OVERRIDE="",
               QT_WAYLAND_DISABLE_WINDOWDECORATION="1")
    env.pop("WAYLAND_DISPLAY", None)
    processes = []
    with (root / "compositor.log").open("w+") as log:
        try:
            server = subprocess.Popen([compositor], env=env, stdout=log, stderr=log)
            processes.append(server)
            sockets = wait_for("the compositor", lambda: server.poll() is None and [
                p for p in root.glob("wayland-*") if not p.name.endswith(".lock")])
            env["WAYLAND_DISPLAY"] = sockets[0].name
            state_file = root / f"zacos9-idle-{sockets[0].name}"

            def states():
                try:
                    lines = state_file.read_text().split("\n")
                except FileNotFoundError:
                    return {}
                return dict(line.split()[1:3] for line in lines if line.startswith("output "))

            def sleeps():
                return slept.read_text().count("slept") if slept.exists() else 0

            def wake():
                subprocess.run([vptr, "move", "3", "3"], env=env, check=True, timeout=10)

            def lit_pixels(output):
                """Pixels on a screen that aren't black, or None without grim."""
                if not shutil.which("grim"):
                    return None
                shot = root / f"{output}.ppm"
                subprocess.run(["grim", "-t", "ppm", "-o", output, str(shot)], env=env,
                               check=True, timeout=10)
                data = shot.read_bytes()
                # P6, width, height, 255, then RGB triples.
                fields, at = [], 2
                while len(fields) < 3:
                    while data[at:at + 1].isspace():
                        at += 1
                    end = at
                    while not data[end:end + 1].isspace():
                        end += 1
                    fields.append(int(data[at:end]))
                    at = end
                pixels = data[at + 1:]
                return sum(1 for i in range(0, len(pixels), 3 * 97) if pixels[i:i + 3] != b"\0\0\0")

            def saver_running():
                for stat in Path("/proc").glob("[0-9]*/stat"):
                    try:
                        text = stat.read_text()
                    except OSError:
                        continue
                    comm = text[text.index("(") + 1:text.rindex(")")]
                    state, parent = text[text.rindex(")") + 2:].split()[:2]
                    if comm.startswith("zacos9-screen") and int(parent) == server.pid and state != "Z":
                        return True
                return False

            wait_for("two screens, awake", lambda: list(states().values()) == ["awake", "awake"])
            wait_for("the screen saver on both", lambda: set(states().values()) == {"saver"})
            wait_for("zacos9-screensaver", saver_running, 2)
            if lit_pixels("HEADLESS-1") is not None:
                wait_for("the Blank Screen saver covering both screens",
                         lambda: lit_pixels("HEADLESS-1") == 0 and lit_pixels("HEADLESS-2") == 0, 1)
                print("ok: the screen saver covers both screens")
            wait_for("both displays asleep", lambda: set(states().values()) == {"asleep"})
            wait_for("the screen saver to leave once they sleep", lambda: not saver_running(), 2)
            wait_for("the system asleep", lambda: sleeps() >= 1)
            print("ok: screen saver, then display sleep, then system sleep")

            wake()
            wait_for("input to wake both", lambda: set(states().values()) == {"awake"}, 2)
            if lit_pixels("HEADLESS-1") is not None:
                assert lit_pixels("HEADLESS-1") > 0 and lit_pixels("HEADLESS-2") > 0, "still dark"
            print("ok: input wakes every screen, the desktop showing again")

            # Woken from the screen saver itself, it goes at once.
            wait_for("the screen saver again", lambda: set(states().values()) == {"saver"})
            wait_for("zacos9-screensaver again", saver_running, 2)
            wake()
            wait_for("input to wake both", lambda: set(states().values()) == {"awake"}, 2)
            if lit_pixels("HEADLESS-1") is not None:
                assert lit_pixels("HEADLESS-1") > 0 and lit_pixels("HEADLESS-2") > 0, \
                    "the screen saver stayed after input"
            wait_for("the screen saver to leave once woken", lambda: not saver_running(), 2)
            print("ok: input takes the screen saver away at once")

            film = subprocess.Popen([client], env=env, stdout=subprocess.PIPE, stderr=log, text=True)
            processes.append(film)
            line = film.stdout.readline()
            assert line.startswith("fullscreen"), f"client never filled a screen: {line!r}"
            film_screen = line.split()[1]
            other = "HEADLESS-1" if film_screen == "HEADLESS-2" else "HEADLESS-2"
            wake()
            before = sleeps()
            wait_for("the screen saver on the other screen only",
                     lambda: states().get(other) == "saver" and states().get(film_screen) == "awake")
            if lit_pixels(other) is not None:
                wait_for("the saver covering the other screen", lambda: lit_pixels(other) == 0, 1)
                assert lit_pixels(film_screen) > 0, "the screen saver covered the film"
                print("ok: the screen saver shows on the idle screen while the film plays on the other")
            wait_for("one screen asleep, the film's awake",
                     lambda: sorted(states().values()) == ["asleep", "awake"])
            time.sleep(8 * MINUTE / 1000)  # past the system's own time
            assert sorted(states().values()) == ["asleep", "awake"], states()
            assert sleeps() == before, "the system slept during a full-screen film"
            print("ok: a full-screen window keeps its screen, and the system, awake; the other sleeps")

            film.terminate()
            film.wait(timeout=5)
            wait_for("both asleep once it closes", lambda: set(states().values()) == {"asleep"})
            wait_for("the system asleep once it closes", lambda: sleeps() > before)
            print("ok: once it closes, its screen and the system sleep too")
        except Exception:
            log.seek(0)
            print(log.read(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()

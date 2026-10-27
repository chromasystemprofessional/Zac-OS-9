#!/usr/bin/python3
import importlib.util
import os
import select
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("SKIP: Pillow is needed for cursor pixel checks")
    sys.exit(77)
if not shutil.which("grim") or not shutil.which("Xwayland"):
    print("SKIP: grim and Xwayland are needed for cursor integration checks")
    sys.exit(77)

compositor, client, pointer, builder, source = map(lambda arg: Path(arg).resolve(), sys.argv[1:6])
# Inside the probe window, which opens near full-screen past the desktop strip.
AT = 300
spec = importlib.util.spec_from_file_location("cursors", builder)
cursors = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cursors)
definitions = {c["name"]: c for c in cursors.parse(source)}
bone = definitions["watch"]
bone_frames = cursors.images(bone)[0][1]


def expect_line(process, expected):
    assert select.select([process.stdout], [], [], 5)[0], f"Timed out waiting for {expected}"
    assert process.stdout.readline().strip() == expected


def command(process, text):
    process.stdin.write(text + "\n")
    process.stdin.flush()
    expect_line(process, "ok")
    time.sleep(0.12)


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


with tempfile.TemporaryDirectory(prefix="zacos9-busy-cursor-") as directory:
    root = Path(directory)
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_CONFIG_HOME=directory,
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
               ZACOS9_STARTUP="0", ZACOS9_MENUBAR="", ZACOS9_FINDER="",
               ZACOS9_COLLAR="",
               QT_QPA_PLATFORM="wayland", QT_STYLE_OVERRIDE="",
               QT_WAYLAND_DISABLE_WINDOWDECORATION="1", XCURSOR_THEME="ZacOS9",
               XCURSOR_SIZE="16", XCURSOR_PATH=str(compositor.parent),
               BUSY_DISPLAY=str(root / "display"))
    env.pop("WAYLAND_DISPLAY", None)
    with (root / "server.log").open("w+") as log:
        server = subprocess.Popen([compositor, "-s", 'printf "%s" "$DISPLAY" > "$BUSY_DISPLAY"'],
                                  env=env, stdout=log, stderr=log)
        probe = None
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                sockets = [p for p in root.glob("wayland-*") if not p.name.endswith(".lock")]
                if sockets and (root / "display").exists():
                    break
                assert server.poll() is None, "Compositor exited before creating its socket"
                time.sleep(0.02)
            assert sockets and (root / "display").exists(), "Compositor did not start"
            env["WAYLAND_DISPLAY"] = sockets[0].name
            env["DISPLAY"] = (root / "display").read_text()
            # Headless backends have no physical devices. A virtual keyboard
            # advertises the seat's pointer capability for toolkit clients.
            subprocess.run([pointer, "key", "Escape"], env=env, check=True,
                           capture_output=True, timeout=5)

            def move():
                subprocess.run([pointer, "home", "move", str(AT), str(AT)], env=env,
                               check=True, capture_output=True, timeout=5)

            def shot():
                path = root / "cursor.png"
                subprocess.run(["grim", "-c", str(path)], env=env, check=True,
                               capture_output=True, timeout=5)
                return Image.open(path).convert("RGB")

            def matches(image, pixels, hot):
                # Only opaque pixels: antialiased edges blend with the window.
                for i, p in enumerate(pixels):
                    if p >> 24 == 255:
                        rgb = (p >> 16 & 255, p >> 8 & 255, p & 255)
                        if image.getpixel((AT - hot[0] + i % 16, AT - hot[1] + i // 16)) != rgb:
                            return False
                return True

            def spinning_bone():
                seen = set()
                seen_frames = set()
                for _ in range(24):
                    image = shot()
                    found = [i for i, frame in enumerate(bone_frames)
                             if matches(image, frame, bone["hot"])]
                    if not found:
                        print(f"Unexpected pointer pixels around ({AT},{AT}):", file=sys.stderr)
                        for y in range(AT - 20, AT + 20):
                            print("".join({(0, 0, 0): "K", (255, 255, 255): "W",
                                           (0, 255, 0): "."}.get(image.getpixel((x, y)), "?")
                                          for x in range(AT - 20, AT + 20)), file=sys.stderr)
                        raise AssertionError("Busy pointer does not match the dog bone artwork")
                    seen.add(image.crop((AT - 8, AT - 8, AT + 8, AT + 8)).tobytes())
                    seen_frames.update(found)
                    time.sleep(0.08)
                assert len(seen) > 1, "Dog bone pointer is not animating"
                assert len(seen_frames) > 4, "Dog bone pointer is not spinning"

            def normal_cursor(name):
                definition = definitions[name]
                frames, _ = cursors.images(definition)
                assert matches(shot(), frames[1][0], definition["hot"]), f"{name} cursor was not restored"

            for platform in ("wayland", "xcb"):
                probe_env = dict(env, QT_QPA_PLATFORM=platform)
                with (root / f"{platform}.log").open("w") as errors:
                    probe = subprocess.Popen([client], env=probe_env, stdin=subprocess.PIPE,
                                             stdout=subprocess.PIPE, stderr=errors, text=True)
                    expect_line(probe, "ready")
                    move()
                    if platform == "wayland":
                        command(probe, "begin")
                        spinning_bone()
                        command(probe, "cancel")
                        normal_cursor("default")
                        command(probe, "begin")
                        command(probe, "show")
                        move()
                        normal_cursor("default")  # Matching window ends launch feedback.
                        command(probe, "failure")
                        normal_cursor("default")
                    else:
                        command(probe, "show")
                        move()
                    command(probe, "wait")
                    spinning_bone()
                    command(probe, "progress")
                    spinning_bone()
                    command(probe, "text")
                    normal_cursor("text")
                    if platform == "wayland":
                        command(probe, "other")
                        move()
                        spinning_bone()
                        command(probe, "cancel")
                        normal_cursor("text")
                    command(probe, "arrow")
                    normal_cursor("default")
                    command(probe, "quit")
                    assert probe.wait(timeout=5) == 0
                    probe = None
                print(f"ok: {platform} dog bone animation and normal cursor restoration")

            # A launcher crash/disconnect must also release its outstanding feedback.
            with (root / "disconnect.log").open("w") as errors:
                probe = subprocess.Popen([client], env=env, stdin=subprocess.PIPE,
                                         stdout=subprocess.PIPE, stderr=errors, text=True)
                expect_line(probe, "ready")
                move()
                command(probe, "begin")
                spinning_bone()
                stop(probe)
                probe = None
                time.sleep(0.12)
                normal_cursor("default")
            print("ok: launch readiness, failure, cancellation and launcher disconnect")
        except Exception:
            log.seek(0)
            print(log.read(), file=sys.stderr)
            for path in root.glob("*.log"):
                if path.name != "server.log":
                    print(path.name + ":\n" + path.read_text(), file=sys.stderr)
            raise
        finally:
            if probe:
                stop(probe)
            stop(server)

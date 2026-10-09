"""Exercise real layer-shell surfaces in a private compositor, not the live desktop."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

from PIL import Image

compositor, collar, capture, vptr = map(os.path.abspath, sys.argv[1:5])
del sys.argv[1:5]


class NativeCollar(unittest.TestCase):
    def exercise(self, scale, collapsed=False):
        with tempfile.TemporaryDirectory(prefix="zacos9-collar-native-") as temporary:
            root = Path(temporary)
            config = root / "config" / "zacos9"
            config.mkdir(parents=True)
            (config / "collar.conf").write_text(
                f"[General]\ncollapsed={'true' if collapsed else 'false'}\nbottom=4\n"
            )
            env = os.environ | {
                "HOME": temporary,
                "XDG_CONFIG_HOME": str(root / "config"),
                "XDG_DATA_HOME": str(root / "data"),
                "XDG_RUNTIME_DIR": temporary,
                "WAYLAND_DISPLAY": "wayland-0",
                "WLR_BACKENDS": "headless",
                "WLR_RENDERER": "pixman",
                "WLR_HEADLESS_OUTPUTS": "1",
                "ZACOS9_FINDER": "",
                "ZACOS9_MENUBAR": "",
                "ZACOS9_COLLAR": "",
                "ZACOS9_STARTUP": "0",
                "QT_QPA_PLATFORM": "wayland",
                "QT_USE_PORTAL": "0",
                "GTK_USE_PORTAL": "0",
                "DBUS_SYSTEM_BUS_ADDRESS": os.environ["DBUS_SESSION_BUS_ADDRESS"],
                "PULSE_SERVER": "unix:" + str(root / "no-sound-server"),
            }
            processes = []
            with (root / "session.log").open("w+") as log:
                try:
                    server = subprocess.Popen([compositor, "-S", str(scale)], env=env,
                                              stdout=log, stderr=log)
                    processes.append(server)
                    deadline = time.monotonic() + 8
                    while not (root / "wayland-0").is_socket():
                        self.assertIsNone(server.poll(), "private compositor exited")
                        self.assertLess(time.monotonic(), deadline, "compositor socket timed out")
                        time.sleep(0.05)
                    client = subprocess.Popen([collar], env=env, stdout=log, stderr=log)
                    processes.append(client)
                    image_path = root / "screen.png"
                    deadline = time.monotonic() + 8
                    while True:
                        self.assertIsNone(client.poll(), "Collar exited before mapping")
                        result = subprocess.run([capture, "--cli", str(image_path)], env=env,
                                                capture_output=True, text=True, timeout=10)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        with Image.open(image_path) as image:
                            pixels = image.convert("RGBA")
                        top = pixels.height - 28 * scale
                        if pixels.getpixel((8 * scale, top)) == (0, 0, 0, 255):
                            break
                        self.assertLess(time.monotonic(), deadline, "Collar never appeared at bottom left")
                        time.sleep(0.1)
                    self.assertEqual(pixels.getpixel((8 * scale, top + scale)), (255, 255, 255, 255))
                    width = (17 if collapsed else 219) * scale
                    background = pixels.getpixel((width + 5 * scale, top + 12 * scale))
                    self.assertNotEqual(pixels.getpixel((2 * scale, top + 12 * scale)), background)
                    self.assertEqual(pixels.getpixel((width, top + 12 * scale)), background,
                                     "strip extends beyond its exact logical width")
                    for dy in range(scale):
                        for dx in range(scale):
                            self.assertEqual(pixels.getpixel((8 * scale + dx, top + scale + dy)),
                                             (255, 255, 255, 255),
                                             "integer scale preserves crisp pixel blocks")
                    duplicate = subprocess.run([collar], env=env, capture_output=True,
                                               text=True, timeout=5)
                    self.assertNotEqual(duplicate.returncode, 0, "duplicate shell component started")
                    self.assertIn("D-Bus name", duplicate.stderr)
                    if not collapsed:
                        # Genuine input is required: Qt turns a Popup into a toplevel
                        # if no Wayland input device/serial has ever been received.
                        pointer = subprocess.Popen(
                            [vptr, "key", "Escape", "home", "move", "79",
                             str(pixels.height // scale - 16), "click", "wait", "1800",
                             "key", "Escape", "wait", "200"],
                            env=env, stdout=log, stderr=log,
                        )
                        processes.append(pointer)
                        time.sleep(0.6)
                        result = subprocess.run([capture, "--cli", str(image_path)], env=env,
                                                capture_output=True, text=True, timeout=10)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        with Image.open(image_path) as image:
                            pixels = image.convert("RGBA")
                        popup_top = pixels.height - (28 + 73) * scale
                        self.assertEqual(pixels.getpixel((63 * scale, popup_top)), (0, 0, 0, 255),
                                         "popup isn't directly above its module on the real display")
                        self.assertEqual(pixels.getpixel((64 * scale, popup_top + scale)),
                                         (255, 255, 255, 255), "popup bevel doesn't map correctly")
                        self.assertEqual(pointer.wait(timeout=5), 0, "virtual input helper failed")
                        result = subprocess.run([capture, "--cli", str(image_path)], env=env,
                                                capture_output=True, text=True, timeout=10)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        with Image.open(image_path) as image:
                            self.assertEqual(image.convert("RGBA").getpixel((63 * scale, popup_top)),
                                             background, "Escape didn't dismiss the real popup")
                        self.assertIsNone(server.poll(), "popup tests killed the compositor")
                except Exception:
                    log.flush()
                    log.seek(0)
                    sys.stderr.write(log.read())
                    raise
                finally:
                    for process in reversed(processes):
                        if process.poll() is None:
                            process.terminate()
                            try:
                                process.wait(timeout=5)
                            except subprocess.TimeoutExpired:
                                process.kill()
                                process.wait(timeout=5)

    def test_normal(self):
        self.exercise(1)

    def test_integer_hidpi(self):
        self.exercise(2)

    def test_collapsed(self):
        self.exercise(1, collapsed=True)


unittest.main()

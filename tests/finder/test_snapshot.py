"""Native captures in an isolated headless compositor; no real screen is read."""

import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import zlib


compositor, capture, wrapper, animation = map(os.path.abspath, sys.argv[1:5])
del sys.argv[1:5]


def png(path):
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    offset, compressed = 8, b""
    width = height = depth = color = None
    while offset < len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        contents = data[offset + 8:offset + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", contents[:10])
        elif kind == b"IDAT":
            compressed += contents
        offset += length + 12
    raw = zlib.decompress(compressed)
    # Decode the first RGBA scanline, enough to verify rendered color changes.
    assert depth == 8 and color == 6
    decoded = bytearray(raw[1:1 + width * 4])
    for index in range(len(decoded)):
        left = decoded[index - 4] if index >= 4 else 0
        if raw[0] in (1, 4):
            decoded[index] = (decoded[index] + left) & 255
        elif raw[0] == 3:
            decoded[index] = (decoded[index] + left // 2) & 255
        else:
            assert raw[0] in (0, 2)
    return width, height, depth, color, bytes(decoded)


class NativeCaptureTests(unittest.TestCase):
    renderer = "pixman"
    scale = 1

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="zacos9-native-snapshot-")
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        self.env = os.environ | {
            "HOME": str(root),
            "XDG_CONFIG_HOME": str(root / "config"),
            "XDG_DATA_HOME": str(root / "data"),
            "XDG_RUNTIME_DIR": str(root),
            "WAYLAND_DISPLAY": "wayland-0",
            "WLR_BACKENDS": "headless",
            "WLR_RENDERER": self.renderer,
            "WLR_HEADLESS_OUTPUTS": "2",
            "ZACOS9_FINDER": "",
            "ZACOS9_MENUBAR": "",
            "ZACOS9_STARTUP": "0",
            "QT_QPA_PLATFORM": "offscreen",
        }
        self.log = open(root / "compositor.log", "w+")
        self.addCleanup(self.log.close)
        self.server = subprocess.Popen(
            [compositor, "-S", str(self.scale)], env=self.env, stdout=self.log, stderr=self.log,
        )
        self.addCleanup(self.stop)
        deadline = time.monotonic() + 8
        while not (root / "wayland-0").is_socket():
            if self.server.poll() is not None or time.monotonic() >= deadline:
                self.log.seek(0)
                self.fail("Headless compositor failed:\n" + self.log.read())
            time.sleep(0.05)
        time.sleep(0.4)

    def stop(self):
        if self.server.poll() is None:
            self.server.terminate()
            try:
                self.server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.server.kill()
                self.server.wait(timeout=5)

    def run_capture(self, *args):
        return subprocess.run(
            [capture, "--cli", *args], env=self.env, text=True,
            capture_output=True, timeout=15,
        )

    def test_repeat_captures_regions_multidisplay_and_failures(self):
        target = Path(self.tmp.name) / "capture.png"
        result = self.run_capture(str(target))
        self.assertEqual(result.returncode, 0, result.stderr)
        width, height, depth, color, pixels = png(target)
        self.assertEqual((depth, color), (8, 6))
        self.assertGreater(width, height)
        self.assertTrue(pixels)
        self.assertEqual(pixels[:4], bytes((0x66, 0x66, 0xCC, 255)))
        app = subprocess.Popen([animation], env=self.env | {"QT_QPA_PLATFORM": "wayland"},
                               stdout=self.log, stderr=self.log)
        def stop_animation():
            if app.poll() is None:
                app.terminate()
                app.wait(timeout=5)
        self.addCleanup(stop_animation)
        time.sleep(0.5)
        colors = set()
        # Repeated region readbacks must not stall subsequent compositor frames.
        for _ in range(8):
            result = self.run_capture("--geometry", "0,0 100x80", str(target))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(png(target)[:2], (100 * self.scale, 80 * self.scale))
            colors.add(png(target)[4][:4])
            self.assertIsNone(self.server.poll())
            self.assertIsNone(app.poll())
            time.sleep(0.07)
        self.assertIn(bytes((255, 0, 0, 255)), colors)
        self.assertIn(bytes((0, 255, 0, 255)), colors)
        original = target.read_bytes()
        for geometry in ("invalid", "0,0 0x80", "99999,0 100x80"):
            result = self.run_capture("--geometry", geometry, str(target))
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(target.read_bytes(), original)
        result = self.run_capture(str(Path(self.tmp.name) / "missing" / "image.png"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Could not save", result.stderr)


@unittest.skipUnless(any(os.access(path, os.R_OK | os.W_OK)
                         for path in Path("/dev/dri").glob("renderD*")),
                     "a GPU render node is required for GLES readback tests")
class GpuCaptureTests(NativeCaptureTests):
    renderer = "gles2"


class HiDpiCaptureTests(NativeCaptureTests):
    scale = 2


class WrapperTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="zacos9-snapshot-wrapper-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.script = self.root / "zacos9-snapshot"
        shutil.copy(wrapper, self.script)
        self.env = os.environ | {
            "HOME": str(self.root), "PATH": str(self.root) + ":/usr/bin:/bin",
            "SNAPSHOT_LOG": str(self.root / "log"),
            "TEST_STATUS": "0",
        }
        self.tool("slurp", 'echo "10,20 30x40"\nexit "${SELECT_STATUS:-0}"')
        self.tool("xdg-user-dir", 'echo "$HOME/Desktop"')
        self.tool("sleep", "exit 0")
        self.tool("zacos9-capture",
                  'echo "capture:$*" >> "$SNAPSHOT_LOG"\n'
                  '[ "$TEST_STATUS" = 0 ] || exit "$TEST_STATUS"\n'
                  'printf "PNG" > "$3"')
        self.tool("wl-copy", 'echo "copy:$*" >> "$SNAPSHOT_LOG"\ncat >/dev/null\n'
                  'exit "${COPY_STATUS:-0}"')
        for name in ("grim", "gnome-screenshot", "xdg-open"):
            self.tool(name, 'echo "UNSAFE" >> "$SNAPSHOT_LOG"\nexit 99')

    def tool(self, name, body):
        path = self.root / name
        path.write_text("#!/bin/sh\n" + body + "\n")
        path.chmod(0o700)

    def run_wrapper(self, **env):
        return subprocess.run(
            [str(self.script)], env=self.env | env, capture_output=True,
            text=True, timeout=5,
        )

    def test_native_numbering_and_clipboard_only(self):
        desktop = self.root / "Desktop"
        desktop.mkdir()
        (desktop / "Picture 1.png").write_text("keep")
        result = self.run_wrapper()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((desktop / "Picture 1.png").read_text(), "keep")
        self.assertEqual((desktop / "Picture 2.png").read_text(), "PNG")
        log = (self.root / "log").read_text()
        self.assertIn("capture:--geometry 10,20 30x40", log)
        self.assertIn("copy:--type image/png", log)
        self.assertNotIn("UNSAFE", log)

    def test_capture_failure_does_not_copy_or_fallback(self):
        result = self.run_wrapper(TEST_STATUS="7")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("copy:", (self.root / "log").read_text())
        self.assertNotIn("UNSAFE", (self.root / "log").read_text())

    def test_cancel_does_not_capture(self):
        result = self.run_wrapper(SELECT_STATUS="1")
        self.assertEqual(result.returncode, 0)
        self.assertFalse((self.root / "log").exists())

    def test_selection_failure_is_reported(self):
        result = self.run_wrapper(SELECT_STATUS="2")
        self.assertEqual(result.returncode, 2)
        self.assertIn("selection failed", result.stderr)
        self.assertFalse((self.root / "log").exists())

    def test_clipboard_failure_preserves_picture_and_reports_error(self):
        result = self.run_wrapper(COPY_STATUS="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue((self.root / "Desktop" / "Picture 1.png").exists())
        self.assertIn("clipboard copying failed", result.stderr)


if __name__ == "__main__":
    unittest.main()

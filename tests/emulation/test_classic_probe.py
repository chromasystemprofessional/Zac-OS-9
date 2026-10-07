"""Host-only tests for the Classic rendering experiment's pixel verifier."""

import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "classic_probe", ROOT / "emulation/seamless/verify_probe.py"
)
probe = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = probe
SPEC.loader.exec_module(probe)


def capture_bytes(window, frame, width=64, height=64, mode="candidate"):
    pixels = bytearray((width + 7) // 8 * height)
    stride = (width + 7) // 8
    for y in range(height):
        for x in range(width):
            black = (((frame >> ((x // 8) % 32)) ^ window) if y < 8 else
                     x // 8 + y // 8 + window + frame) & 1
            pixels[y * stride + x // 8] |= black << (7 - x % 8)
    return (f"P4\n# zacos9-classic-probe 1 {mode} {window} {frame}\n"
            f"{width} {height}\n").encode("ascii") + pixels


def graphics_bytes(window, frame, mode="candidate"):
    return capture_bytes(window, frame, 320, 192, mode).replace(
        b"probe 1 ", b"probe 2 ", 1
    ).replace(f" {frame}\n".encode(), f" {frame} graphics\n".encode(), 1)


class ProbeTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def captures(self, frame=33, width=64, height=64, mode="candidate"):
        paths = [self.root / f"window-{window}.pbm" for window in (0, 1)]
        for window, path in enumerate(paths):
            path.write_bytes(capture_bytes(window, frame, width, height, mode))
        return paths

    def test_exact_pair_and_resized_non_byte_aligned_width(self):
        for width, height in ((64, 64), (320, 192), (319, 191), (640, 384)):
            with self.subTest(width=width, height=height):
                captures = probe.verify_pair(self.captures(width=width, height=height),
                                             33, "candidate")
                self.assertEqual(len(captures), 2)

    def test_cropped_front_window_cannot_pass_as_obscured_window(self):
        paths = self.captures()
        front = probe.read_capture(paths[1])
        old = paths[0].read_bytes()
        paths[0].write_bytes(old[:-len(front.pixels)] + front.pixels)
        with self.assertRaisesRegex(ValueError, "pixels differ"):
            probe.verify_pair(paths, 33, "candidate")

    def test_stale_frame_rejected_even_with_correct_old_pixels(self):
        with self.assertRaisesRegex(ValueError, "stale/wrong frame"):
            probe.verify_pair(self.captures(frame=32), 33, "candidate")

    def test_relabelled_stale_pixels_are_rejected(self):
        paths = self.captures(frame=32)
        for path in paths:
            path.write_bytes(path.read_bytes().replace(b" 32\n", b" 33\n", 1))
        with self.assertRaisesRegex(ValueError, "pixels differ"):
            probe.verify_pair(paths, 33, "candidate")

    def test_single_pixel_corruption_rejected(self):
        paths = self.captures()
        data = bytearray(paths[0].read_bytes())
        data[-1] ^= 1
        paths[0].write_bytes(data)
        with self.assertRaisesRegex(ValueError, "1 pixels differ"):
            probe.verify_pair(paths, 33, "candidate")

    def test_duplicate_window_and_wrong_mode_rejected(self):
        paths = self.captures()
        with self.assertRaisesRegex(ValueError, "each probe window"):
            probe.verify_pair([paths[0], paths[0]], 33, "candidate")
        with self.assertRaisesRegex(ValueError, "mode"):
            probe.verify_pair(paths, 33, "reference")

    def test_helper_candidates_require_matching_independent_references(self):
        candidates = self.captures()
        references = [self.root / f"reference-{window}.pbm" for window in (0, 1)]
        for window, path in enumerate(references):
            path.write_bytes(capture_bytes(window, 33, mode="reference"))
        self.assertEqual(len(probe.verify_against_references(candidates, references, 33)), 2)
        references[0].write_bytes(capture_bytes(0, 33, width=65, mode="reference"))
        with self.assertRaisesRegex(ValueError, "dimensions"):
            probe.verify_against_references(candidates, references, 33)
        references[0].write_bytes(capture_bytes(0, 32, mode="reference"))
        with self.assertRaisesRegex(ValueError, "stale/wrong frame"):
            probe.verify_against_references(candidates, references, 33)
        references[0].write_bytes(capture_bytes(0, 33, mode="candidate"))
        with self.assertRaisesRegex(ValueError, "mode"):
            probe.verify_against_references(candidates, references, 33)

    def test_graphics_requires_references_and_checks_all_pixels(self):
        paths = [self.root / f"candidate-{w}.pbm" for w in (0, 1)]
        references = [self.root / f"reference-{w}.pbm" for w in (0, 1)]
        for window in (0, 1):
            data = bytearray(graphics_bytes(window, 33))
            data[-100] ^= 0x53
            paths[window].write_bytes(data)
            references[window].write_bytes(bytes(data).replace(b"candidate", b"reference", 1))
        with self.assertRaisesRegex(ValueError, "independent reference"):
            probe.verify_pair(paths, 33, "candidate")
        probe.verify_against_references(paths, references, 33)
        data = bytearray(paths[0].read_bytes())
        data[-1] ^= 1
        paths[0].write_bytes(data)
        with self.assertRaisesRegex(ValueError, "1 pixels differ"):
            probe.verify_against_references(paths, references, 33)
        references[0].write_bytes(capture_bytes(0, 33, 320, 192, "reference"))
        with self.assertRaisesRegex(ValueError, "profile"):
            probe.verify_against_references(paths, references, 33)

    def test_graphics_markers_reject_identically_relabelled_stale_pairs(self):
        candidates = [self.root / f"candidate-{w}.pbm" for w in (0, 1)]
        references = [self.root / f"reference-{w}.pbm" for w in (0, 1)]
        for window in (0, 1):
            candidates[window].write_bytes(
                graphics_bytes(window, 32).replace(b" 32 graphics", b" 33 graphics", 1)
            )
            references[window].write_bytes(
                graphics_bytes(window, 32, "reference").replace(b" 32 graphics", b" 33 graphics", 1)
            )
        with self.assertRaisesRegex(ValueError, "pixels differ"):
            probe.verify_against_references(candidates, references, 33)

    def test_invalid_graphics_version_profile_combinations(self):
        path = self.root / "capture.pbm"
        for data in (graphics_bytes(0, 33).replace(b"probe 2", b"probe 1", 1),
                     graphics_bytes(0, 33).replace(b" graphics", b"", 1)):
            path.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "version/profile"):
                probe.read_capture(path)

    def test_graphics_cli_requires_references(self):
        candidates = [self.root / f"candidate-{w}.pbm" for w in (0, 1)]
        references = [self.root / f"reference-{w}.pbm" for w in (0, 1)]
        for w in (0, 1):
            candidates[w].write_bytes(graphics_bytes(w, 33))
            references[w].write_bytes(graphics_bytes(w, 33, "reference"))
        command = [sys.executable, "-B", str(ROOT / "emulation/seamless/verify_probe.py"),
                   "--frame", "33", "--mode", "candidate"]
        result = subprocess.run(command + list(map(str, candidates)),
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("independent reference", result.stderr)
        result = subprocess.run(command + ["--references", *map(str, references),
                                          *map(str, candidates)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        candidates[0].write_bytes(candidates[0].read_bytes().replace(b"320 192", b"255 192", 1))
        with self.assertRaisesRegex(ValueError, "too narrow"):
            probe.read_capture(candidates[0])

    def test_invalid_inputs_rejected(self):
        path = self.root / "bad.pbm"
        valid = capture_bytes(0, 33)
        for data in (b"P4\n", valid[:-1], valid + b"x",
                     valid.replace(b"64 64", b"999 999"),
                     valid.replace(b" 33\n", b" 4294967296\n"),
                     bytes(probe.MAX_FILE_BYTES + 1)):
            with self.subTest(length=len(data)):
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    probe.read_capture(path)

    def test_guest_c_pattern_matches_independent_host_oracle(self):
        compiler = shutil.which("cc")
        self.assertIsNotNone(compiler, "a C compiler is required for the guest pattern test")
        source = self.root / "pattern-test.c"
        source.write_text(
            '#include <stdio.h>\n#include "pattern.h"\n'
            'int main(void) { unsigned int id, f; int x, y;\n'
            'unsigned long frames[] = {0, 1, 32, 33, 0xffffffffUL};\n'
            'for(id=0; id<2; id++) { for(f=0; f<5; f++) {\n'
            'for(y=0; y<64; y++) { for(x=0; x<64; x++) {\n'
            'putchar(probe_black(id, frames[f], x, y)); } } } } return 0; }\n',
            encoding="ascii",
        )
        binary = self.root / "pattern-test"
        subprocess.run([compiler, "-Wall", "-Wextra", "-Werror", "-I",
                        str(ROOT / "emulation/seamless/probe"),
                        str(source), "-o", str(binary)], check=True)
        actual = subprocess.check_output([str(binary)])
        expected = bytearray()
        for window in (0, 1):
            for frame in (0, 1, 32, 33, 0xffffffff):
                for y in range(64):
                    for x in range(64):
                        expected.append((((frame >> ((x // 8) % 32)) ^ window)
                                         if y < 8 else
                                         x // 8 + y // 8 + window + frame) & 1)
        self.assertEqual(actual, expected)

    def test_cli_failure_and_success_are_explicit(self):
        command = [sys.executable, str(ROOT / "emulation/seamless/verify_probe.py"),
                   "--frame", "33", "--mode", "candidate"]
        result = subprocess.run(command + list(map(str, self.captures())),
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("not the seamless rendering feasibility gate", result.stdout)
        references = [self.root / f"reference-{window}.pbm" for window in (0, 1)]
        for window, path in enumerate(references):
            path.write_bytes(capture_bytes(window, 33, mode="reference"))
        result = subprocess.run(command + ["--references", *map(str, references),
                                          *map(str, self.captures())],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        references[0].write_bytes(capture_bytes(0, 33, width=65, mode="reference"))
        result = subprocess.run(command + ["--references", *map(str, references),
                                          *map(str, self.captures())],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("dimensions", result.stderr)
        result = subprocess.run(command + ["missing-a", "missing-b"],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FAIL:", result.stderr)


if __name__ == "__main__":
    unittest.main()

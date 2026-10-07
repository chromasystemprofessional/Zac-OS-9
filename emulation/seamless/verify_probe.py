"""Verify the original Classic probe's PBM exports, not arbitrary PBM images."""

import argparse
from dataclasses import dataclass
from pathlib import Path
import re


MAX_WIDTH = 640
MAX_HEIGHT = 384
MAX_FILE_BYTES = MAX_WIDTH * MAX_HEIGHT // 8 + 256
HEADER = re.compile(
    rb"P4\n# zacos9-classic-probe ([12]) (reference|screen|candidate) ([01]) "
    rb"([0-9]{1,10})( graphics)?\n([0-9]{1,3}) ([0-9]{1,3})\n"
)


@dataclass(frozen=True)
class Capture:
    mode: str
    window: int
    frame: int
    width: int
    height: int
    pixels: bytes
    profile: str = "pattern"


def read_capture(path):
    with Path(path).open("rb") as source:
        data = source.read(MAX_FILE_BYTES + 1)
    if len(data) > MAX_FILE_BYTES:
        raise ValueError(f"{path}: capture exceeds the fixture size limit")
    header = HEADER.match(data)
    if header is None:
        raise ValueError(f"{path}: invalid or unsupported probe header")
    version, mode, window, frame, graphics, width, height = header.groups()
    if (version == b"2") != bool(graphics):
        raise ValueError(f"{path}: invalid fixture version/profile combination")
    window, frame, width, height = map(int, (window, frame, width, height))
    if frame > 0xffffffff or not 64 <= width <= MAX_WIDTH or not 64 <= height <= MAX_HEIGHT:
        raise ValueError(f"{path}: invalid frame number or dimensions")
    if graphics and width < 256:
        raise ValueError(f"{path}: graphics capture is too narrow for the complete frame marker")
    pixels = data[header.end():]
    if len(pixels) != ((width + 7) // 8) * height:
        raise ValueError(f"{path}: truncated or trailing pixel data")
    return Capture(mode.decode("ascii"), window, frame, width, height, pixels,
                   "graphics" if graphics else "pattern")


def mismatch_count(capture):
    if capture.profile != "pattern":
        raise ValueError("graphics captures require independent reference comparison")
    mismatches = 0
    stride = (capture.width + 7) // 8
    for y in range(capture.height):
        for x in range(capture.width):
            if y < 8:
                expected = ((capture.frame >> ((x // 8) % 32)) ^ capture.window) & 1
            else:
                expected = (x // 8 + y // 8 + capture.window + capture.frame) & 1
            actual = (capture.pixels[y * stride + x // 8] >> (7 - x % 8)) & 1
            mismatches += actual != expected
    return mismatches


def verify_pair(paths, frame, mode, allow_graphics=False):
    if mode not in ("reference", "screen", "candidate"):
        raise ValueError("unknown capture mode")
    captures = [read_capture(path) for path in paths]
    if len(captures) != 2 or {capture.window for capture in captures} != {0, 1}:
        raise ValueError("provide exactly one capture for each probe window (0 and 1)")
    for capture in captures:
        if capture.frame != frame:
            raise ValueError(
                f"window {capture.window}: stale/wrong frame {capture.frame}; expected {frame}"
            )
        if capture.mode != mode:
            raise ValueError(
                f"window {capture.window}: mode {capture.mode}; expected {mode}"
            )
        if capture.profile == "graphics":
            if not allow_graphics:
                raise ValueError("graphics captures require independent reference comparison")
            stride = (capture.width + 7) // 8
            mismatches = sum(
                ((capture.pixels[y * stride + x // 8] >> (7 - x % 8)) & 1) !=
                (((frame >> (x // 8)) ^ capture.window) & 1)
                for y in range(8) for x in range(min(capture.width, 256))
            )
        else:
            mismatches = mismatch_count(capture)
        if mismatches:
            raise ValueError(
                f"window {capture.window}: {mismatches} pixels differ from frame {frame}"
            )
    return captures


def verify_against_references(paths, reference_paths, frame, mode="candidate"):
    captures = verify_pair(paths, frame, mode, allow_graphics=True)
    references = {
        capture.window: capture
        for capture in verify_pair(reference_paths, frame, "reference", allow_graphics=True)
    }
    for capture in captures:
        reference = references[capture.window]
        if capture.profile != reference.profile:
            raise ValueError(f"window {capture.window}: candidate profile does not match reference")
        if (capture.width, capture.height) != (reference.width, reference.height):
            raise ValueError(
                f"window {capture.window}: candidate dimensions do not match reference"
            )
        stride = (capture.width + 7) // 8
        differing = sum(
            ((capture.pixels[y * stride + x // 8] ^
              reference.pixels[y * stride + x // 8]) >> (7 - x % 8)) & 1
            for y in range(capture.height) for x in range(capture.width)
        )
        if differing:
            raise ValueError(
                f"window {capture.window}: {differing} pixels differ from independent reference"
            )
    return captures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frame", type=int, required=True,
                        help="expected sequence number from the experiment, not a guessed frame")
    parser.add_argument("--mode", choices=("reference", "screen", "candidate"), required=True)
    parser.add_argument("--references", nargs=2, type=Path,
                        help="independent fixture exports to compare with helper candidates")
    parser.add_argument("captures", nargs=2, type=Path)
    args = parser.parse_args()
    if not 0 <= args.frame <= 0xffffffff:
        parser.error("frame must be an unsigned 32-bit sequence number")
    if args.references and args.mode == "reference":
        parser.error("--references requires --mode candidate or screen")
    try:
        captures = (verify_against_references(args.captures, args.references, args.frame, args.mode)
                    if args.references else
                    verify_pair(args.captures, args.frame, args.mode))
    except (OSError, ValueError) as error:
        parser.exit(1, f"FAIL: {error}\n")
    for capture in captures:
        print(f"PASS: window {capture.window}, frame {capture.frame}, "
              f"{capture.width}x{capture.height}, exact pixels ({capture.mode})")
    print("This verifies these captures only, not the seamless rendering feasibility gate.")


if __name__ == "__main__":
    main()

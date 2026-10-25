#!/usr/bin/python3
import importlib.util
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

builder, source = map(Path, sys.argv[1:3])
spec = importlib.util.spec_from_file_location("cursors", builder)
cursors = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cursors)
definitions = cursors.parse(source)
bone = next(c for c in definitions if c["name"] == "watch")
assert bone["spin"] and bone["hot"] == (8, 8)
frames, delay = cursors.images(bone)
assert delay == 60 and all(len(frames[s]) == 24 for s in (1, 2))
for scale, sequence in frames.items():
    assert len(set(map(tuple, sequence))) == 24
    for frame in sequence:
        assert any(p >> 24 == 255 for p in frame), "Bone frame has no opaque pixels"
        assert len({p & 0xFFFFFF for p in frame if p >> 24 == 255}) > 8, "Bone lost its rainbow"


def red_side(frame, size):
    """Mean offset from the centre of the bone's red (gradient start) pixels."""
    red = [(i % size - size / 2, i // size - size / 2) for i, p in enumerate(frame)
           if p >> 24 == 255 and (p >> 16 & 255) > 200 and (p & 255) < 90 and (p >> 8 & 255) < 120]
    return sum(x for x, _ in red) / len(red), sum(y for _, y in red) / len(red)


# Red starts on the left; clockwise on screen (y down) carries it to the top, then right.
x, y = red_side(frames[2][0], 32)
assert x < -6 and abs(y) < 3
x, y = red_side(frames[2][6], 32)
assert y < -6 and abs(x) < 3
x, y = red_side(frames[2][12], 32)
assert x > 6 and abs(y) < 3
with tempfile.TemporaryDirectory(prefix="zacos9-cursors-") as directory:
    subprocess.run([sys.executable, builder, source, directory], check=True)
    theme = Path(directory) / "ZacOS9" / "cursors"
    data = (theme / "watch").read_bytes()
    magic, header, version, count = struct.unpack_from("<4sIII", data)
    assert (magic, header, version, count) == (b"Xcur", 16, 0x10000, 24 * len(cursors.SCALES))
    for name in bone["aliases"]:
        assert (theme / name).read_bytes() == data, name
    for i in range(count):
        kind, nominal, offset = struct.unpack_from("<III", data, 16 + 12 * i)
        image = struct.unpack_from("<9I", data, offset)
        scale = cursors.SCALES[i // 24]
        size = cursors.snap(16, scale)
        assert image == (36, kind, nominal, 1, size, size,
                         cursors.snap(8, scale), cursors.snap(8, scale), 60)
        pixels = struct.unpack_from(f"<{size * size}I", data, offset + 36)
        assert list(pixels) == frames[scale][i % 24]
    arrow = (theme / "default").read_bytes()
    assert struct.unpack_from("<I", arrow, 12)[0] == len(cursors.SCALES), "Normal pointer must remain static"
    # 1.25x, 1.5x, 1.75x: sizes of their own, each 1x pixel one or two whole ones.
    sizes = [struct.unpack_from("<III", arrow, 16 + 12 * i)[1] for i in range(len(cursors.SCALES))]
    assert sizes == [16, 20, 24, 28, 32], sizes
print("ok: dog bone spins clockwise; aliases, hotspots, timing and HiDPI pixels")

# The Mouse panel's Thick I-Beam: the text cursor, twice as thick, with
# no other names (zacos9-wm picks it for "text" itself).
thin = next(c for c in definitions if c["name"] == "text")
thick = next(c for c in definitions if c["name"] == "text-thick")
stem = lambda c: c["rows"][8].count("K")  # the middle row
assert thick["aliases"] == [] and thick["hot"] == thin["hot"]
assert (stem(thin), stem(thick)) == (1, 2), (stem(thin), stem(thick))
print("ok: the thick I-beam is the text cursor, thicker")

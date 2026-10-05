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
dog = next(c for c in definitions if c["name"] == "watch")
frames = cursors.watch_frames(dog["rows"], dog["hot"])
assert len(frames) == 24
assert len({tuple(map(tuple, frame)) for frame in frames[:12]}) == 3
assert len({tuple(map(tuple, frame)) for frame in frames[12:]}) >= 10
assert frames[0] != frames[12]
assert all(any("K" in row for row in frame) for frame in frames)
with tempfile.TemporaryDirectory(prefix="zacos9-cursors-") as directory:
    subprocess.run([sys.executable, builder, source, directory], check=True)
    theme = Path(directory) / "ZacOS9" / "cursors"
    data = (theme / "watch").read_bytes()
    magic, header, version, count = struct.unpack_from("<4sIII", data)
    assert (magic, header, version, count) == (b"Xcur", 16, 0x10000, 48)
    for name in dog["aliases"]:
        assert (theme / name).read_bytes() == data, name
    for i in range(count):
        kind, nominal, offset = struct.unpack_from("<III", data, 16 + 12 * i)
        image = struct.unpack_from("<9I", data, offset)
        scale = 1 if i < 24 else 2
        assert image == (36, kind, nominal, 1, 16 * scale, 16 * scale,
                         7 * scale, 7 * scale, 80)
        pixels = struct.unpack_from(f"<{256 * scale * scale}I", data, offset + 36)
        expected = frames[i % 24]
        for y in range(16 * scale):
            for x in range(16 * scale):
                assert pixels[y * 16 * scale + x] == cursors.COLORS[expected[y // scale][x // scale]]
    arrow = (theme / "default").read_bytes()
    assert struct.unpack_from("<I", arrow, 12)[0] == 2, "Normal pointer must remain static"
print("ok: original tail-wag/backflip animation, aliases, hotspots, timing and HiDPI pixels")

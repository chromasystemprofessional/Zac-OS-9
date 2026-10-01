"""Build the Platinum Xcursor theme from assets/cursors/platinum-cursors.pcur.

usage: build_cursors.py SOURCE OUTDIR [--sheet OUT.png]

Writes OUTDIR/Platinum/index.theme and OUTDIR/Platinum/cursors/<name> for
every cursor name and alias, each with 16 px (1x) and 32 px (2x,
nearest-neighbour) images. The watch is animated: its hand turns in
eight steps.
"""
import math
import os
import struct
import sys

COLORS = {"K": 0xFF000000, "W": 0xFFFFFFFF, ".": 0}
WATCH_FRAMES, WATCH_MS = 8, 100


def parse(path):
    cursors, cur = [], None
    with open(path, encoding="utf-8") as f:
        for n, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            if not line.strip() or line.startswith(";"):
                continue
            words = line.split()
            if words[0] == "cursor":
                outline = "outline" in words[4:]
                names = [w for w in words[4:] if w != "outline"]
                cur = {"name": words[1], "hot": (int(words[2]), int(words[3])),
                       "outline": outline, "aliases": names, "rows": []}
                cursors.append(cur)
            elif cur is not None and set(line) <= set(COLORS):
                cur["rows"].append(list(line))
            else:
                sys.exit(f"{path}:{n}: unexpected line")
    for c in cursors:
        if len(c["rows"]) != 16 or any(len(r) != 16 for r in c["rows"]):
            sys.exit(f"{path}: cursor {c['name']} must be 16 rows of 16")
    return cursors


def add_outline(rows):
    out = [r[:] for r in rows]
    for y in range(16):
        for x in range(16):
            if rows[y][x] != ".":
                continue
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    yy, xx = y + dy, x + dx
                    if 0 <= yy < 16 and 0 <= xx < 16 and rows[yy][xx] == "K":
                        out[y][x] = "W"
    return out


def watch_frames(rows, hot):
    """The face with its hand at eight angles (12 o'clock first)."""
    frames = []
    cx, cy = hot
    for i in range(WATCH_FRAMES):
        f = [r[:] for r in rows]
        a = i * 2 * math.pi / WATCH_FRAMES
        for t in range(4):
            x = round(cx + 0.5 + math.sin(a) * t)
            y = round(cy + 0.5 - math.cos(a) * t)
            if f[y][x] == "W":
                f[y][x] = "K"
        f[cy][cx] = "K"
        frames.append(f)
    return frames


def image_chunk(rows, nominal, scale, hot, delay):
    size = 16 * scale
    pixels = bytearray()
    for y in range(size):
        for x in range(size):
            pixels += struct.pack("<I", COLORS[rows[y // scale][x // scale]])
    header = struct.pack("<9I", 36, 0xFFFD0002, nominal, 1, size, size,
                         hot[0] * scale, hot[1] * scale, delay)
    return header + bytes(pixels)


def xcursor(frames, hot, delay):
    chunks = []
    for scale in (1, 2):
        for f in frames:
            chunks.append((16 * scale, image_chunk(f, 16 * scale, scale, hot, delay)))
    ntoc = len(chunks)
    out = bytearray(struct.pack("<4sIII", b"Xcur", 16, 0x10000, ntoc))
    pos = 16 + ntoc * 12
    for nominal, data in chunks:
        out += struct.pack("<III", 0xFFFD0002, nominal, pos)
        pos += len(data)
    for _, data in chunks:
        out += data
    return bytes(out)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    cursors = parse(sys.argv[1])
    theme = os.path.join(sys.argv[2], "Platinum")
    os.makedirs(os.path.join(theme, "cursors"), exist_ok=True)
    with open(os.path.join(theme, "index.theme"), "w") as f:
        f.write("[Icon Theme]\nName=Platinum\nComment=Platinum 2026 cursors\n")
    rendered = []
    for c in cursors:
        rows = add_outline(c["rows"]) if c["outline"] else c["rows"]
        if c["name"] == "watch":
            frames, delay = watch_frames(rows, c["hot"]), WATCH_MS
        else:
            frames, delay = [rows], 0
        data = xcursor(frames, c["hot"], delay)
        for name in [c["name"]] + c["aliases"]:
            with open(os.path.join(theme, "cursors", name), "wb") as f:
                f.write(data)
        rendered.append(frames[0])
    stamp = os.path.join(sys.argv[2], "cursors.stamp")
    with open(stamp, "w") as f:
        f.write("\n".join(c["name"] for c in cursors) + "\n")
    if len(sys.argv) == 5 and sys.argv[3] == "--sheet":
        from PIL import Image
        sheet = Image.new("RGB", (len(rendered) * 20 + 4, 44), (0x66, 0x66, 0xCC))
        px = sheet.load()
        for i, rows in enumerate(rendered):
            for y in range(16):
                for x in range(16):
                    v = COLORS[rows[y][x]]
                    for oy in (2, 24):
                        bg = (0x66, 0x66, 0xCC) if oy == 2 else (255, 255, 255)
                        if oy == 24:
                            px[4 + i * 20 + x, oy + y] = bg
                        if v:
                            c = (v >> 16) & 255
                            px[4 + i * 20 + x, oy + y] = (c, c, c)
        for x in range(sheet.width):
            for y in range(22, 44):
                if px[x, y] == (0x66, 0x66, 0xCC):
                    px[x, y] = (255, 255, 255)
        sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(sys.argv[4])


if __name__ == "__main__":
    main()

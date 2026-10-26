"""Build the ZacOS9 Xcursor theme from assets/cursors/platinum-cursors.pcur.

usage: build_cursors.py SOURCE OUTDIR [--sheet OUT.png]

Writes OUTDIR/ZacOS9/index.theme and OUTDIR/ZacOS9/cursors/<name> for
every cursor name and alias, each with an image for every pixel size the
Monitors panel offers: 16 px (1x), 20 (1.25x), 24 (1.5x), 28 (1.75x),
32 (2x) and 48 (3x). zacos9-wm shows the one for the screen's scale, so
the pointer is the same size on the desktop as in programs that draw
their own. Pixel-art cursors are scaled nearest-neighbour, at the
fractions each 1x pixel one or two whole pixels by lib/draw.c's pl_snap
rounding (sharp, as the rest of the screen); "spin" cursors (the busy dog
bone) are drawn from their SVG at each size and rotate clockwise.
"""
import math
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

COLORS = {"K": 0xFF000000, "W": 0xFFFFFFFF, ".": 0}
SCALES = (1, 1.25, 1.5, 1.75, 2, 3)
SPIN_FRAMES = 24
SPIN_MS = 60
SVG = "{http://www.w3.org/2000/svg}"


def parse(path):
    cursors, cur = [], None
    with open(path, encoding="utf-8") as f:
        for n, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            if not line.strip() or line.startswith(";"):
                continue
            words = line.split()
            if words[0] == "cursor":
                spin = None
                if len(words) > 5 and words[4] == "spin":
                    spin = os.path.join(os.path.dirname(path), words[5])
                    del words[4:6]
                outline = "outline" in words[4:]
                names = [w for w in words[4:] if w != "outline"]
                cur = {"name": words[1], "hot": (int(words[2]), int(words[3])),
                       "outline": outline, "aliases": names, "rows": [], "spin": spin}
                cursors.append(cur)
            elif cur is not None and set(line) <= set(COLORS):
                cur["rows"].append(list(line))
            else:
                sys.exit(f"{path}:{n}: unexpected line")
    for c in cursors:
        if c["spin"]:
            if c["rows"]:
                sys.exit(f"{path}: spin cursor {c['name']} takes no rows")
        elif len(c["rows"]) != 16 or any(len(r) != 16 for r in c["rows"]):
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


def snap(u, scale):
    """lib/draw.c pl_snap: where 1x pixel u starts at a scale (in quarters there)."""
    return (u * round(scale * 4) + 2) // 4


def unsnap(d, scale):
    """The 1x pixel that pixel d shows (pl_unsnap)."""
    return -((-(4 * d + 2)) // round(scale * 4)) - 1


def pixels(scale):
    """The image size for SCALE: 16 px at 1x."""
    return snap(16, scale)


def pixel_art(rows, scale):
    """Premultiplied ARGB pixels of 16x16 rows, scaled nearest-neighbour."""
    size = pixels(scale)
    return [COLORS[rows[unsnap(y, scale)][unsnap(x, scale)]] for y in range(size) for x in range(size)]


def load_svg(path):
    """The one closed path of PATH as a polygon, plus its paint."""
    root = ET.parse(path).getroot()
    stops = []
    for stop in root.iter(SVG + "stop"):
        offset = stop.get("offset")
        offset = float(offset[:-1]) / 100 if offset.endswith("%") else float(offset)
        stops.append((offset, int(stop.get("stop-color")[1:], 16)))
    shape = next(root.iter(SVG + "path"))
    tokens = re.findall(r"[A-Za-z]|-?\d*\.?\d+", shape.get("d"))
    poly, i, cmd = [], 0, None
    while i < len(tokens):
        if tokens[i].isalpha():
            cmd = tokens[i]
            i += 1
        if cmd in ("M", "L"):
            poly.append((float(tokens[i]), float(tokens[i + 1])))
            i += 2
        elif cmd == "C":
            (x0, y0), pts = poly[-1], [float(t) for t in tokens[i:i + 6]]
            i += 6
            for k in range(1, 13):
                t = k / 12
                a, b, c, d = (1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t * t, t ** 3
                poly.append((a * x0 + b * pts[0] + c * pts[2] + d * pts[4],
                             a * y0 + b * pts[1] + c * pts[3] + d * pts[5]))
        elif cmd in ("Z", "z"):
            cmd = None
        else:
            sys.exit(f"{path}: unsupported path command {cmd}")
    return {"poly": poly, "stops": stops,
            "stroke": int(shape.get("stroke", "#000000")[1:], 16),
            "width": float(shape.get("stroke-width", "1"))}


def gradient(stops, t):
    t = min(max(t, 0.0), 1.0)
    for (o0, c0), (o1, c1) in zip(stops, stops[1:]):
        if t <= o1:
            f = (t - o0) / (o1 - o0) if o1 > o0 else 0.0
            return tuple(round(((c0 >> s) & 255) * (1 - f) + ((c1 >> s) & 255) * f)
                         for s in (16, 8, 0))
    c = stops[-1][1]
    return ((c >> 16) & 255, (c >> 8) & 255, c & 255)


def spin_frames(shape, size, frames=SPIN_FRAMES):
    """SIZE x SIZE premultiplied ARGB frames of SHAPE turning once clockwise."""
    poly, half = shape["poly"], shape["width"] / 2
    xs, ys = [p[0] for p in poly], [p[1] for p in poly]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    # Scale so the shape and its stroke stay inside the image at any angle.
    scale = size / 2 / (max(math.hypot(x - cx, y - cy) for x, y in poly) + half)
    # Paint a master image in SVG units at 8 samples per output pixel.
    m = scale * 8
    ox, oy = x0 - half, y0 - half
    w, h = math.ceil((x1 - x0 + 2 * half) * m), math.ceil((y1 - y0 + 2 * half) * m)
    master = [[None] * w for _ in range(h)]
    edges = list(zip(poly, poly[1:] + poly[:1]))
    for j in range(h):
        v = oy + (j + 0.5) / m
        cross = sorted(ax + (v - ay) * (bx - ax) / (by - ay)
                       for (ax, ay), (bx, by) in edges if (ay <= v) != (by <= v))
        for a, b in zip(cross[::2], cross[1::2]):
            for i in range(max(0, math.ceil((a - ox) * m - 0.5)),
                           min(w, math.floor((b - ox) * m - 0.5) + 1)):
                master[j][i] = gradient(shape["stops"], (ox + (i + 0.5) / m - x0) / (x1 - x0))
    stroke = tuple((shape["stroke"] >> s) & 255 for s in (16, 8, 0))
    for (ax, ay), (bx, by) in edges:
        dx, dy = bx - ax, by - ay
        length = dx * dx + dy * dy or 1.0
        for j in range(max(0, int((min(ay, by) - half - oy) * m)),
                       min(h, int((max(ay, by) + half - oy) * m) + 2)):
            v = oy + (j + 0.5) / m
            for i in range(max(0, int((min(ax, bx) - half - ox) * m)),
                           min(w, int((max(ax, bx) + half - ox) * m) + 2)):
                u = ox + (i + 0.5) / m
                t = min(max(((u - ax) * dx + (v - ay) * dy) / length, 0.0), 1.0)
                if math.hypot(u - ax - t * dx, v - ay - t * dy) <= half:
                    master[j][i] = stroke
    out = []
    for n in range(frames):
        angle = 2 * math.pi * n / frames
        cos, sin = math.cos(angle), math.sin(angle)
        pixels = []
        for py in range(size):
            for px in range(size):
                r = g = b = a = 0
                for sy in range(4):
                    for sx in range(4):
                        X = px + (sx + 0.5) / 4 - size / 2
                        Y = py + (sy + 0.5) / 4 - size / 2
                        # Screen y points down, so this inverse turns clockwise.
                        u = cx + (cos * X + sin * Y) / scale
                        v = cy + (-sin * X + cos * Y) / scale
                        i, j = int((u - ox) * m), int((v - oy) * m)
                        if 0 <= i < w and 0 <= j < h and master[j][i]:
                            c = master[j][i]
                            r, g, b, a = r + c[0], g + c[1], b + c[2], a + 1
                pixels.append(((a * 255 + 8) // 16) << 24 | (r // 16) << 16
                              | (g // 16) << 8 | b // 16)
        out.append(pixels)
    return out


def images(cursor):
    """{scale: [frame pixels]} and the frame delay for CURSOR."""
    if cursor["spin"]:
        shape = load_svg(cursor["spin"])
        return {s: spin_frames(shape, pixels(s)) for s in SCALES}, SPIN_MS
    rows = add_outline(cursor["rows"]) if cursor["outline"] else cursor["rows"]
    return {s: [pixel_art(rows, s)] for s in SCALES}, 0


def xcursor(frames, hot, delay):
    chunks = []
    for scale in SCALES:
        size = pixels(scale)
        for frame in frames[scale]:
            header = struct.pack("<9I", 36, 0xFFFD0002, size, 1, size, size,
                                 snap(hot[0], scale), snap(hot[1], scale), delay)
            chunks.append((size, header + struct.pack(f"<{len(frame)}I", *frame)))
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
    theme = os.path.join(sys.argv[2], "ZacOS9")
    os.makedirs(os.path.join(theme, "cursors"), exist_ok=True)
    with open(os.path.join(theme, "index.theme"), "w") as f:
        f.write("[Icon Theme]\nName=ZacOS9\nComment=ZacOS 9 cursors\n")
    rendered = []
    for c in cursors:
        frames, delay = images(c)
        data = xcursor(frames, c["hot"], delay)
        for name in [c["name"]] + c["aliases"]:
            with open(os.path.join(theme, "cursors", name), "wb") as f:
                f.write(data)
        rendered.append(frames[1][0])
    stamp = os.path.join(sys.argv[2], "cursors.stamp")
    with open(stamp, "w") as f:
        f.write("\n".join(c["name"] for c in cursors) + "\n")
    if len(sys.argv) == 5 and sys.argv[3] == "--sheet":
        from PIL import Image
        sheet = Image.new("RGB", (len(rendered) * 20 + 4, 44), (255, 255, 255))
        sheet.paste((0x66, 0x66, 0xCC), (0, 0, sheet.width, 22))
        px = sheet.load()
        for i, pixels in enumerate(rendered):
            for oy in (2, 24):
                for y in range(16):
                    for x in range(16):
                        v, at = pixels[y * 16 + x], (4 + i * 20 + x, oy + y)
                        px[at] = tuple(((v >> s) & 255) + c * (255 - (v >> 24)) // 255
                                       for s, c in zip((16, 8, 0), px[at]))
        sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(sys.argv[4])


if __name__ == "__main__":
    main()

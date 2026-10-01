"""Generate Platinum 2026's desktop patterns (assets/patterns/*.png).

usage: make_patterns.py [--sheet OUT.png | --check]

Every pattern is original and reproducible: seamless 64x64 tiles made from
seeded noise, embossed under a top-left light and quantised to a small
palette (as Mac OS 8's 8-bit patterns were), plus 8x8 two-colour classics.
The list lives in PATTERNS, in menu order, the first being the default.
Writes assets/patterns/*.png (for viewing and the Appearance panel) and
lib/patterns_data.h (palette-indexed tiles, used by lib/patterns.c).
"""
import math
import os
import random
import sys

from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "assets", "patterns")
N = 64


def value_noise(seed, cells):
    """Seamless value noise on an N x N tile with `cells` lattice cells."""
    rnd = random.Random(seed)
    lat = [[rnd.random() for _ in range(cells)] for _ in range(cells)]

    def smooth(t):
        return t * t * (3 - 2 * t)

    out = [[0.0] * N for _ in range(N)]
    for y in range(N):
        fy = y * cells / N
        y0 = int(fy)
        ty = smooth(fy - y0)
        for x in range(N):
            fx = x * cells / N
            x0 = int(fx)
            tx = smooth(fx - x0)
            a = lat[y0 % cells][x0 % cells]
            b = lat[y0 % cells][(x0 + 1) % cells]
            c = lat[(y0 + 1) % cells][x0 % cells]
            d = lat[(y0 + 1) % cells][(x0 + 1) % cells]
            out[y][x] = (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty
    return out


def fractal(seed, octaves):
    """Octaves of value noise, as (cells, weight) pairs."""
    h = [[0.0] * N for _ in range(N)]
    for i, (cells, w) in enumerate(octaves):
        n = value_noise(seed * 31 + i, cells)
        for y in range(N):
            for x in range(N):
                h[y][x] += n[y][x] * w
    return h


def worley(seed, points):
    """Seamless cellular noise: distance to the nearest feature point."""
    rnd = random.Random(seed)
    pts = [(rnd.random() * N, rnd.random() * N) for _ in range(points)]
    h = [[0.0] * N for _ in range(N)]
    for y in range(N):
        for x in range(N):
            best = 1e9
            for px, py in pts:
                dx = min(abs(x - px), N - abs(x - px))
                dy = min(abs(y - py), N - abs(y - py))
                best = min(best, math.hypot(dx, dy))
            h[y][x] = -best
    return h


def emboss(h, strength):
    """Lighting factor per pixel from a top-left light, around 1.0."""
    out = [[1.0] * N for _ in range(N)]
    for y in range(N):
        for x in range(N):
            gx = h[y][(x + 1) % N] - h[y][(x - 1) % N]
            gy = h[(y + 1) % N][x] - h[(y - 1) % N][x]
            out[y][x] = 1.0 - strength * (gx + gy)
    return out


def tint(light, base, levels, spread):
    """Map lighting to `levels` shades of `base` (an RGB triple)."""
    flat = sorted(v for row in light for v in row)
    lo, hi = flat[len(flat) // 50], flat[-len(flat) // 50]
    img = Image.new("RGB", (N, N))
    px = img.load()
    for y in range(N):
        for x in range(N):
            t = (light[y][x] - lo) / (hi - lo or 1)
            t = min(1.0, max(0.0, t))
            q = round(t * (levels - 1)) / (levels - 1)
            k = 1.0 + (q - 0.5) * spread
            px[x, y] = tuple(min(255, max(0, round(c * k))) for c in base)
    return img


def textured(seed, base, kind, levels=12, spread=0.7, strength=6.0):
    if kind == "crumple":
        h = fractal(seed, [(4, 1.0), (8, 0.6), (16, 0.35), (32, 0.2)])
    elif kind == "fine":
        h = fractal(seed, [(16, 1.0), (32, 0.7)])
    elif kind == "stone":
        h = worley(seed, 14)
        noise = fractal(seed, [(16, 2.0)])
        h = [[h[y][x] / 8 + noise[y][x] for x in range(N)] for y in range(N)]
    elif kind == "ripple":
        noise = fractal(seed, [(4, 1.0)])
        h = [[math.sin((x + y * 0.5) * 2 * math.pi / 16 * 1 + noise[y][x] * 4) for x in range(N)]
             for y in range(N)]
    elif kind == "tiles":
        h = [[(1.0 if 2 <= x % 16 <= 13 and 2 <= y % 16 <= 13 else 0.0) for x in range(N)]
             for y in range(N)]
        noise = fractal(seed, [(16, 0.15)])
        h = [[h[y][x] + noise[y][x] for x in range(N)] for y in range(N)]
    elif kind == "streak":
        noise = fractal(seed, [(4, 1.0), (8, 0.5)])
        h = [[math.sin((x - y) * 2 * math.pi / 8 + noise[y][x] * 6) * 0.5 for x in range(N)]
             for y in range(N)]
    else:
        raise ValueError(kind)
    return tint(emboss(h, strength / 8), base, levels, spread)


def classic(rows, colors):
    img = Image.new("RGB", (8, 8))
    px = img.load()
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            px[x, y] = colors[c]
    return img


PATTERNS = [
    ("Platinum", lambda: textured(1, (0x66, 0x66, 0xCC), "crumple", 10, 0.45, 5.0)),
    ("Ocean Ripple", lambda: textured(2, (0x22, 0x99, 0xAA), "ripple", 10, 0.6, 3.0)),
    ("Slate", lambda: textured(3, (0x88, 0x88, 0x90), "stone", 12, 0.8, 4.0)),
    ("Sandstone", lambda: textured(4, (0xCC, 0xAA, 0x77), "crumple", 12, 0.5)),
    ("Graphite", lambda: textured(5, (0x55, 0x55, 0x5A), "fine", 8, 0.6, 5.0)),
    ("Grape Tiles", lambda: textured(6, (0x88, 0x55, 0xAA), "tiles", 8, 0.7, 4.0)),
    ("Meadow", lambda: textured(7, (0x44, 0x88, 0x44), "streak", 10, 0.6, 3.0)),
    ("Lavender Weave", lambda: classic([
        "d.......", "........", "....d...", "........",
        "..d.....", "........", "......d.", "........",
    ], {".": (0x66, 0x66, 0xCC), "d": (0x55, 0x55, 0xAA)})),
    ("Gray Dither", lambda: classic([
        "#.#.#.#.", ".#.#.#.#", "#.#.#.#.", ".#.#.#.#",
        "#.#.#.#.", ".#.#.#.#", "#.#.#.#.", ".#.#.#.#",
    ], {".": (0xAA, 0xAA, 0xAA), "#": (0x88, 0x88, 0x88)})),
    ("Solid Gray", lambda: classic(["........"] * 8, {".": (0x99, 0x99, 0x99)})),
]


def slug(name):
    return name.lower().replace(" ", "-")


CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"


def emit(tiles):
    lines = [
        "/* Generated by tools/patterns/make_patterns.py. Do not edit. */",
        "",
    ]
    entries = []
    for i, (name, img) in enumerate(tiles):
        colors = [c for _, c in sorted(img.getcolors(1 << 16), key=lambda nc: nc[1])]
        index = {c: CHARS[k] for k, c in enumerate(colors)}
        px = img.load()
        lines.append(f"static const uint32_t pattern_{i}_palette[] = {{")
        lines.append("\t" + ", ".join("0xFF%02X%02X%02X" % c for c in colors) + ",")
        lines.append("};")
        lines.append(f"static const char *const pattern_{i}_rows[] = {{")
        for y in range(img.height):
            lines.append('\t"' + "".join(index[px[x, y]] for x in range(img.width)) + '",')
        lines.append("};")
        entries.append(f'\t{{ "{slug(name)}", "{name}", {img.width}, {img.height}, '
                       f"pattern_{i}_palette, pattern_{i}_rows }},")
    lines.append("static const struct pattern patterns[] = {")
    lines += entries
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def main():
    tiles = [(name, make()) for name, make in PATTERNS]
    header = emit(tiles)
    out_h = os.path.join(ROOT, "lib", "patterns_data.h")
    if sys.argv[1:] == ["--check"]:
        with open(out_h) as f:
            if f.read() != header:
                sys.exit("lib/patterns_data.h is out of date: run tools/patterns/make_patterns.py")
        return
    with open(out_h, "w") as f:
        f.write(header)
    os.makedirs(OUT, exist_ok=True)
    for name, img in tiles:
        img.save(os.path.join(OUT, slug(name) + ".png"), optimize=True)
        print(f"{name}: {img.width}x{img.height}, {len(img.getcolors(1 << 16))} colors")
    if len(sys.argv) == 3 and sys.argv[1] == "--sheet":
        cell = 132
        sheet = Image.new("RGB", (cell * 5, cell * ((len(tiles) + 4) // 5)), "white")
        for i, (name, img) in enumerate(tiles):
            t = Image.new("RGB", (128, 128))
            for x in range(0, 128, img.width):
                for y in range(0, 128, img.height):
                    t.paste(img, (x, y))
            sheet.paste(t, ((i % 5) * cell + 2, (i // 5) * cell + 2))
        sheet.save(sys.argv[2])


if __name__ == "__main__":
    main()

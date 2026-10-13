#!/usr/bin/env python3
"""Regenerates the menu bar's 20x15 Apple-menu icon (logo16_data in
lib/logo.c, logo_pixels()) from Happy Zac's head, drawn on its own in
assets/menubar/happy-zac-mini.png: trimmed to its outline, scaled to fit
20x15 px with its proportions kept and centred on transparency. Needs Pillow;
run from anywhere:

    python3 tools/menubar/make-menu-logo.py

The result is committed, so a build needs no Pillow.
"""
import os
import re

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
W, H = 20, 15                # PL_LOGO_W/H, MBAR_ICON_W/H

head = Image.open(os.path.join(ROOT, "assets", "menubar", "happy-zac-mini.png")).convert("RGBA")
# Trim to the clearly visible drawing, past its faint halo, and drop the halo.
alpha = head.getchannel("A").point(lambda v: v if v > 8 else 0)
head.putalpha(alpha)
head = head.crop(alpha.getbbox())
scale = min(W / head.size[0], H / head.size[1])
w, h = (max(1, round(n * scale)) for n in head.size)
# Premultiplied while resampling, so transparent pixels don't darken the edge.
head = head.convert("RGBa").resize((w, h), Image.LANCZOS).convert("RGBA")
icon = Image.new("RGBA", (W, H), (0, 0, 0, 0))
icon.paste(head, ((W - w) // 2, (H - h) // 2))
pixels = ["0x%02X%02X%02X%02X" % (a, r, g, b) for r, g, b, a in icon.getdata()]
rows = ["    " + ", ".join(pixels[y * W:(y + 1) * W]) + "," for y in range(H)]
table = "static const uint32_t logo16_data[PL_LOGO_W * PL_LOGO_H] = {\n" + "\n".join(rows) + "\n};"

path = os.path.join(ROOT, "lib", "logo.c")
source = open(path).read()
source, n = re.subn(r"static const uint32_t logo16_data\[PL_LOGO_W \* PL_LOGO_H\] = \{.*?\n\};", table,
                    source, count=1, flags=re.S)
if n != 1:
    raise SystemExit("make-menu-logo: logo16_data not found in lib/logo.c")
open(path, "w").write(source)
print("wrote logo16_data in lib/logo.c")

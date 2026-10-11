#!/usr/bin/env python3
"""Regenerates the menu bar's 16x16 Apple-menu icon (logo16_data in
lib/logo.c, logo_pixels()) from Happy Zac: a close-up of the dog's head,
cropped from assets/boot/happy-zac.png. Needs Pillow; run from anywhere:

    python3 tools/menubar/make-menu-logo.py

The result is committed, so a build needs no Pillow.
"""
import os
import re

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SIZE = 16                    # PL_LOGO_SIZE, MBAR_ICON_SIZE
CROP = (150, 80, 400, 330)   # the dog's head and ears, in the 512 px master

master = Image.open(os.path.join(ROOT, "assets", "boot", "happy-zac.png")).convert("RGBA")
icon = master.crop(CROP).resize((SIZE, SIZE), Image.LANCZOS)
pixels = ["0x%02X%02X%02X%02X" % (a, r, g, b) for r, g, b, a in icon.getdata()]
rows = ["    " + ", ".join(pixels[y * SIZE:(y + 1) * SIZE]) + "," for y in range(SIZE)]
table = "static const uint32_t logo16_data[16 * 16] = {\n" + "\n".join(rows) + "\n};"

path = os.path.join(ROOT, "lib", "logo.c")
source = open(path).read()
source, n = re.subn(r"static const uint32_t logo16_data\[16 \* 16\] = \{.*?\n\};", table,
                    source, count=1, flags=re.S)
if n != 1:
    raise SystemExit("make-menu-logo: logo16_data not found in lib/logo.c")
open(path, "w").write(source)
print("wrote logo16_data in lib/logo.c")

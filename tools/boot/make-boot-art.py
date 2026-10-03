#!/usr/bin/env python3
"""Regenerates the boot screen art from boot/plymouth/logo.png (itself made
from the ZacOS 9 SVG). Needs Pillow; run from anywhere:

    python3 tools/boot/make-boot-art.py

Writes (and the results are committed, so a build needs no Pillow):
  boot/logo-64.rgb                              the logo flattened on white
  iso/config/bootloaders/grub-pc/zacos9-boot.png      GRUB on the live medium (800x600)
  iso/config/bootloaders/isolinux/splash.png          BIOS boot menu (640x480)
"""
import os
import subprocess
import sys

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def p(*parts):
    return os.path.join(ROOT, *parts)


logo = Image.open(p("boot", "plymouth", "logo.png")).convert("RGBA")
assert logo.size == (64, 64), logo.size
flat = Image.new("RGBA", logo.size, (255, 255, 255, 255))
flat.alpha_composite(logo)
with open(p("boot", "logo-64.rgb"), "wb") as f:
    f.write(flat.convert("RGB").tobytes())

tool = p("boot", "zacos9-bootlogo")
for w, h, out in ((800, 600, p("iso", "config", "bootloaders", "grub-pc", "zacos9-boot.png")),
                  (640, 480, p("iso", "config", "bootloaders", "isolinux", "splash.png"))):
    subprocess.run([sys.executable, tool, str(w), str(h), out], check=True)
    print("wrote", os.path.relpath(out, ROOT))

#!/usr/bin/env python3
"""Regenerates the boot screen art from the two masters in assets/boot
(512x512, transparent around the art). Needs Pillow; run from anywhere:

    python3 tools/boot/make-boot-art.py

Writes (and the results are committed, so a build needs no Pillow):
  assets/boot/happy-zac-Nx.png    Happy Zac, the start-up picture on white
  assets/boot/zacos-logo-Nx.png   the logo in the Welcome to Zacintosh box
      both 32*N pixels square for N = 1..6: the startup screen is drawn at
      the 1984 Macintosh's size scaled by a whole N (lib/welcome.h)
  iso/config/bootloaders/grub-pc/zacos9-boot.png      GRUB on the live medium (800x600)
  iso/config/bootloaders/isolinux/splash.png          BIOS boot menu (640x480)
"""
import os
import subprocess
import sys

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ICON = 32       # PL_WELCOME_ICON
MAX_SCALE = 6   # PL_WELCOME_MAX_SCALE


def p(*parts):
    return os.path.join(ROOT, *parts)


for name in ("happy-zac", "zacos-logo"):
    master = Image.open(p("assets", "boot", name + ".png")).convert("RGBA")
    for n in range(1, MAX_SCALE + 1):
        out = p("assets", "boot", f"{name}-{n}x.png")
        master.resize((ICON * n, ICON * n), Image.LANCZOS).save(out, optimize=True)
        print("wrote", os.path.relpath(out, ROOT))

tool = p("boot", "zacos9-bootlogo")
env = dict(os.environ, ZACOS9_BOOT_ART=p("assets", "boot"))
for w, h, out in ((800, 600, p("iso", "config", "bootloaders", "grub-pc", "zacos9-boot.png")),
                  (640, 480, p("iso", "config", "bootloaders", "isolinux", "splash.png"))):
    subprocess.run([sys.executable, tool, str(w), str(h), out], check=True, env=env)
    print("wrote", os.path.relpath(out, ROOT))

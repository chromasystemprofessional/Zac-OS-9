"""Nearest-neighbour upscale of HIG figures for viewing, plus palette dump."""
import sys
from PIL import Image

for name in sys.argv[1:]:
    im = Image.open(f"figs/{name}.png")
    rgb = im.convert("RGB")
    colors = sorted(rgb.getcolors(1 << 16), reverse=True)
    print(name, im.size, im.mode, "colors:",
          " ".join("#%02x%02x%02x(%d)" % (c[1] + (c[0],)) for c in colors[:16]))
    scale = max(1, min(8, 1600 // im.size[0]))
    rgb.resize((im.size[0] * scale, im.size[1] * scale), Image.NEAREST).save(f"figs/{name}-x{scale}.png")

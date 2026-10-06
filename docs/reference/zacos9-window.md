# Platinum document window: measured reference

Source: Apple, *Mac OS 8 Human Interface Guidelines* (Developer Note, 9/2/97),
chapter 5 "Window Guidelines". The figures in that PDF are unscaled, indexed-color
screen captures. Every value below was read pixel by pixel from them using
`tools/measure/` (see "Reproducing" at the end).

| Figure | PDF page | Embedded image | Used for |
|---|---|---|---|
| 5-1 Active vs inactive window | 100 | `img-100-128` 435×233 | frame, content well, resize box, inactive state |
| 5-6 Collapsed window | 104 | `img-104-134` 232×23 | title bar, boxes, stripes, title text, shadow |

Apple's bitmaps are **not** stored in this repo. This document records measurements only.

## Conventions

- Coordinates are **frame-local**. `(0,0)` is the outer border's top-left pixel.
  `W`×`H` is the frame size **excluding** the drop shadow.
- Colors are Mac 8-bit system palette values. The grids below use one character
  per pixel, where a hex digit `n` means gray `#nnnnnn` (for example `9` is `#999999`).

| Name | Active | Inactive |
|---|---|---|
| Outer border, content-well line | `#000000` | `#555555` |
| Frame fill | `#CCCCCC` | `#DDDDDD` |
| Highlight (top and left bevel) | `#FFFFFF` | none |
| Shade (bottom and right bevel) | `#999999` | none |
| Drop shadow | `#000000` | `#555555` |
| Title text | `#000000` | `#666666` |
| Stripes | `#FFFFFF` / `#777777` | none |
| Close, zoom and collapse boxes | drawn | not drawn |

## Frame geometry

Decoration margins around the client surface: **left 6, right 6, top 22, bottom 6.**

Wayland xdg scene trees already place their origin at the window geometry's
top-left. Position that tree directly at the decoration margins; subtracting
the geometry offset again pulls content into the frame. Fullscreen uses a
zero inset. Movable-modal dialogs use margins **3/3/24/3** instead.

New main windows from other programs open nearly full-screen: the frame sits
96 px in from the left and right of the area below the menu bar and 32 px in
from its top and bottom, so the desktop icon column stays visible. Client
size limits still apply. Finder windows, dialogs, windows with a parent,
fixed-size windows and fullscreen requests keep their own size and placement.

```
row/col                       active                         inactive
outer border    x=0, x=W-1, y=0, y=H-1       #000             #555
highlight       y=1  x=1..W-3                #FFF             (fill)
                x=1  y=1..H-3                #FFF             (fill)
shade           x=W-2 y=2..H-2               #999             (fill)
                y=H-2 x=2..W-2               #999             (fill)
title bar       y=1..19 (19 px, per HIG "19 pixels")
well shadow     y=20 x=4..W-6                #999             (fill)
                x=4  y=20..H-6               #999             (fill)
well line       rect x=5..W-6, y=21..H-6     #000 outline     #555 outline
well highlight  x=W-5 y=21..H-5              #FFF             (fill)
                y=H-5 x=4..W-5               #FFF             (fill)
client surface  x=6..W-7, y=22..H-7
drop shadow     x=W   y=2..H                 #000             #555
                y=H   x=2..W                 #000             #555
everything else                              #CCC             #DDD
```

Figure 5-1 shows `#777` and `#888` at `(0,H)` and `(1,H)`, where the shadow row
begins. Figure 5-6 and every other shadow corner show plain background there, so
those two pixels are treated as an artifact of the figure.

Collapsed (WindowShade) window: only the title bar remains. Height is **22**
(`y=0` border, `y=1..19` title bar, `y=20` shade row, `y=21` border), plus the
shadow row, for 23 in total.

## Title-bar controls (active only)

All three boxes are 12×12 at `y=4..15`, with a 1-px white emboss on their right
(`x=bx+12, y=by+1..by+12`) and bottom (`y=by+12, x=bx+1..bx+12`).

| Box | x range | HIG placement |
|---|---|---|
| Close | `4..15` | far left |
| Zoom | `W-33..W-22` | left of the collapse box |
| Collapse | `W-17..W-6` | far right |

Base box (close box shown, `bx=4, by=4`, including emboss):

```
888888888888c
822222222222f
82fcccccccc2f
82c99aabbc82f
82c9aabbcc82f
82caabbccd82f
82cabbccdd82f
82cbbccdde82f
82cbccddee82f
82cccddeef82f
82c888888882f
822222222222f
cffffffffffff
```

- The 9×9 interior has a white top-left pixel, then `#CCC` along its top row and
  left column, and `#888` along its bottom row and right column.
- Inner 7×7 gradient: `value(i,j) = 0x9 + floor((i+j)/2)` (gray nibble), with `i,j = 0..6`.
- **Zoom glyph:** `#222` column at `bx+7, y=by+2..by+7` plus `#222` row at
  `by+7, x=bx+2..bx+7`. This draws a small box in the top-left corner, over the gradient.
- **Collapse glyph:** `#222` rows at `by+5` and `by+7`, spanning `x=bx+1..bx+11`.

## Stripes (active only)

- Six stripe pairs on rows `y=4..15`.
  - Even rows (`4,6,…,14`) are `#FFF` spanning `[x0, x1-1]`.
  - Odd rows (`5,…,15`) are `#777` spanning `[x0+1, x1]`. So the gray is offset 1 px right.
- Left group: `x0 = 21` (4 px clear after the close box emboss), `x1 = inkL - 5`.
- Right group: `x0 = inkR + 5`, `x1 = W-38` (4 px clear before the zoom box).
- `inkL` and `inkR` are the leftmost and rightmost **ink** columns of the title. So
  there is exactly 4 px of clear fill between the stripes and the title on each side.

## Title text

- **Font:** the system font, Charcoal 12 under Platinum. Our original replacement must
  match these metrics: cap height 9, x-height 7, descender 2, and stems 2 px wide (bold look).
- **Baseline:** `y = 14`. Cap tops are at `y = 6` and descenders reach `y = 16`. The same
  values hold for active and inactive windows.
- **Horizontal:** the ink is centred on the frame, `(inkL + inkR) / 2 = (W-1) / 2`.
- Long titles are truncated. Rule not shown in the figures. *TODO: measure.*

## Resize box (active)

The resize box sits at the client's bottom-right corner as a 16×16 cell at
`x=W-21..W-6, y=H-21..H-6`. It has a black line on its top and left edges only, and
its right and bottom are open into the frame (the frame's white well highlight
continues through it). When the client has no scroll bars, the compositor draws this
cell over the client. Grid from `(W-21, H-21)` through `(W-2, H-2)`:

```
0000000000000000fcc9    (y=H-21)
0ffffffffffffffffcc9
0fccccccccccccccccc9
0fccccccccccccccccc9
0fcccccccffcccccccc9
0fccccccfc7cccccccc9
0fcccccfc7cffcccccc9
0fccccfc7cfc7cccccc9
0fcccfc7cfc7cffcccc9
0fccfc7cfc7cfc7cccc9
0fcca7cfc7cfc7ccccc9
0fccccfc7cfc7cccccc9
0fcccca7cfc7ccccccc9
0fccccccfc7cccccccc9
0fcccccca7ccccccccc9
0fccccccccccccccccc9    (y=H-6: the well line stops at the box)
ffccccccccccccccccc9    (y=H-5)
ccccccccccccccccccc9
ccccccccccccccccccc9
99999999999999999999    (y=H-2)
```

The grip is three diagonal ridges. Each is a `#FFF`/`#777` pair stepping up and to
the right, and each ridge's lower-left end is capped with `#AAA`.

When inactive, the cell is plain `#DDD` with `#555` top and left lines.

## Behaviour (HIG text)

- Windows can be moved by dragging **anywhere in the drag region**: the title bar
  and the gray frame on all sides.
- **Collapse box:** the content disappears but the title bar stays visible and
  active. Clicking again restores the window. A sound plays on collapse and expand
  (can be turned off in Appearance).
  - Option-clicking a collapse box collapses all open windows, or expands all
    collapsed windows if that window was collapsed.
  - Optional setting: double-click the title bar to collapse.
  - Moving a collapsed window moves where it reopens.
- **Zoom box:** sits to the left of the collapse box. Variants are full, vertical
  only, and horizontal only.
- **Modeless dialogs:** a document window without the resize box, zoom box or scroll
  bars. The close box is on the left and the collapse box on the right.
- **Utility windows:** the title bar is at least 19 px tall, and a crosshatch pattern
  fills the drag region. *TODO: measure from Figure 5-4.*

## Not yet measured

- Pressed (mouse-down) states of the close, zoom and collapse boxes.
- Utility-window crosshatch, alert and movable-modal frames.
- Title truncation and minimum window width.

## Reproducing

```sh
# inside WSL, from tools/measure/
./fetch.sh          # downloads the HIG PDF, extracts every figure + text
python3 grid.py img-104-134 0 0 60 23    # prints a pixel grid
```

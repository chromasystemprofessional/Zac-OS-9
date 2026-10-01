# Platinum menu bar and menus: measured reference

Source: Apple, *Mac OS 8 Human Interface Guidelines* (9/2/97), chapter 4.
Values were read pixel by pixel from the native-resolution figures using
`tools/measure/` (`grid.py` for pixel grids, `ink.py` for text extents).

| Figure | PDF page | Image | Shows |
|---|---|---|---|
| 4-1 Menu bar using platinum appearance | 91 | `img-091-125` 256×165 | menu bar, rounded screen corner, open Edit menu, disabled items, separators, ⌘ shortcuts |
| 4-2 Help menu | 92 | `img-092-126` 129×84 | highlighted title at the far left, menu width rule, shortcut column |
| 4-3 A contextual menu | 94 | `img-094-127` 244×191 | contextual menu, checkmarks, submenu arrow |

Grays use the same notation as `platinum-window.md`: a hex digit `n` means `#nnnnnn`.

## Menu bar

The bar is **20 px** tall, at screen rows `y = 0..19`.

| Row | Color |
|---|---|
| 0 | `#FFF` highlight |
| 1..17 | `#DDD` fill |
| 18 | `#999` shade |
| 19 | `#000` line (also the top border of any open menu) |

Column 0 is a `#FFF` highlight below the rounded corner (rows 7..17), and row 18
starts with `#DDD` at x=0.

### Rounded screen corner (top-left, 9×9)

```
000005adf
0005afffd
005dfdddd
05dfddddd
0afdddddd
5fddddddd
afddddddd
dfddddddd
fdddddddd
```

The `5`, `a` and `d` pixels antialias the black screen mask; the white highlight
follows the curve. The other three screen corners are not shown in the HIG.
*TODO: measure them; until then the top-right corner mirrors this pattern
without the highlight column.*

### Titles

- Text: the system font (Charcoal 12). Cap tops at `y=5`, baseline `y=13`,
  descenders reach `y=15`. These are the same metrics as window titles.
- Spacing: each title's ink starts **16 px after the previous title's last ink
  column** (15 clear pixels). Figure 4-1 has
  ink at File 43–63, Edit 79–?, View 117–146, Special 162–205 and Help 221–246.
- Apple menu icon: color icon with ink at `x=17..28`, rows 2–16. The first text
  title's ink starts at `x=43`.
- Highlighted (open) title: a box from `ink_l - 10` to `ink_r + 10`, with white text.
  Figure 4-1 has Edit at 69–111; figure 4-2 has Help at 4–49. The box has a
  one-row bevel and no side edges:

  | Row | Color | Accent role |
  |---|---|---|
  | 0 (replaces the white line) | `#6666CC` | light |
  | 1..17 | `#333399` | base |
  | 18 (replaces the shade line) | `#000088` | dark |

  The black line at row 19 is untouched. These three are the default Appearance
  accent. Other accents need their own triples (not a fixed offset); *TODO:
  collect them.*

## Pull-down menu

The menu hangs from the menu bar. Its top border **is** the menu bar's black line
(`y=19`), and its left border aligns with the highlighted title's left edge.
The coordinates below are menu-local: `(0,0)` is the top-left border pixel, and
`W`×`H` excludes the shadow.

```
border     x=0, x=W-1, y=0 (shared with the menu bar), y=H-1    #000
highlight  y=1  x=1..W-3      #FFF
           x=1  y=1..H-3      #FFF
shade      x=W-2 y=2..H-2     #999
           y=H-2 x=2..W-2     #999   (x=1 at that row is #DDD)
fill                          #DDD
shadow     x=W   y=2..H       #222   (1 px)
           y=H   x=2..W       #222
```

Unlike the window frame, the shadow is `#222`, not black.

### Rows

Content rows run from `y=1` to `y=H-2`. The bevel lines are painted over the
first and last rows.

| Row type | Height | Detail |
|---|---|---|
| Item | 16 | text baseline at item top + 11 |
| Separator | 6 | `#888` line at +2, `#FFF` line at +3, spanning `x=1..W-2` |

So `H = 2 + Σ row heights`. In figure 4-1, Undo, a separator, 6 items, a
separator and Preferences give `2 + 16·8 + 6·2 = 142`.

### Columns

- Item text ink starts at **x=18**.
- Shortcuts: the ⌘ glyph (9×9) has its ink at **x = W-28** (27 px left of the
  right border column `W-1`). The key character's ink starts 11 px after the
  ⌘ ink (at `W-17`).
- **Width:** the widest item's ink ends at **`W-13`** (12 px clear of the right
  border). Measured as `W = 18 + ink_width + 12`: 129 for figure 4-1
  ("Show Clipboard") and 122 for figure 4-2 ("Show Balloons").
- Items with shortcuts need room for the shortcut column too. The minimum gap
  between text ink and the ⌘ is not shown in the figures. *TODO: measure;
  12 px assumed.*

### Item states

| State | Text color |
|---|---|
| Enabled | `#000` |
| Disabled | `#888` (shortcut too) |

Figures 4-1 and 4-2 don't show a highlighted (selected) item. We assume the
title's style: a `#333399` fill across `x=1..W-2` over the item's 16 rows, with
white text. *TODO: verify.*

### ⌘ glyph (figure 4-1, disabled color)

```
.##...##.
#..#.#..#
#..#.#..#
.#######.
...#.#...
.#######.
#..#.#..#
#..#.#..#
.##...##.
```

### Checkmark (figure 4-3)

The checkmark is 9×8 with its ink at `x=4..12`, and its bottom row sits on the
item's baseline. It uses the item's text color.

```
........#
.......##
......##.
.....##..
#...##...
##.##....
.###.....
..#......
```

### Submenu arrow (figure 4-3)

The arrow is a solid triangle in the item's text color, 6 wide and 11 tall,
with widths 1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1 from top to bottom. Its ink starts
at `x = W-19` (on figure 4-3's "Label" item) and occupies item rows `+2..+12`.

## Not in the HIG

The HIG shows no clock or Application menu, and no menu-tracking timings. Until
they're measured, these follow Mac OS 8.5 behaviour as observed:

- **Application menu:** at the far right. Its title is the frontmost app's
  16×16 icon (optionally followed by its name). The menu lists the running
  apps, plus Hide «App», Hide Others and Show All.
- **Clock:** left of the Application menu, with the text `h:mm AM`.
- **Tracking:** press on a title to open; drag to an item and release to
  choose; release on the title and the menu stays open until the next click
  (Mac OS 8 "sticky" menus); chosen items blink before the menu closes.

## Trademark note

The Apple menu's title is the Apple logo, which is Apple's trademark. Platinum
2026 uses its own original icon in that position.

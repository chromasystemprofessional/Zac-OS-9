# Mac OS 8/9 theme resource format

This is the reference for `tools/extract-macos9-theme.py` and the Theme
Installer: how an Appearance Manager theme is laid out, so the extractor can
be checked against a spec instead of guesswork. It is reverse-engineered from
two sources that can be read without shipping Apple material:

- **Gizmo** (an Apple Appearance theme) and the other themes in the user's own
  `40 Mac OS 8-9 Themes` folder — their resource forks are the ground truth for
  the canonical resource IDs and the actual bytes.
- **scheme2theme** (Ben Boldt), the open Kaleidoscope→Appearance converter,
  whose `layo.h`/`layo.cpp` document the `layo` layout and the canonical
  control IDs.

Apple's own `Appearance.r` / `Types.r` (the Rez templates for `layo`, `frme`,
`pxm#`, `plut`, `ppat` and `wnd#`) are in the **Appearance SDK 1.0.4** on
Macintosh Garden / Macintosh Repository. They do not ship here and are not
read here; the fields below match them.

## Two formats

The word "theme" covers two distinct resource-fork formats:

- **Appearance themes** (Apple's own, e.g. Gizmo, Hi-Tech, Drawing Board):
  `layo`, `frme`, `pxm#`, `plut`, `ppat`, `scen`, `tdat`, `tic#`, `clr#`.
  The controls are drawn from `frme`/`layo` frames over a `plut` colour
  palette.
- **Kaleidoscope schemes** (third-party): `cicn`/`ics8` icons, `cinf`
  control-info records, `wnd#` window recipes. The frame art is a `cicn`, not
  a `layo`.

Gizmo is an Appearance theme. Kaleidoscope themes are handled by the separate
`write_k2_scrollbars` / `write_wnd_chrome` paths in the extractor.

## Canonical resource IDs

The Appearance Manager refers to every control and window part by a fixed
resource ID. Gizmo's names record them:

### Window frames — `layo` 128–143

| layo | window |
|---|---|
| 128 / 129 | document window (active / inactive) |
| 130 / 131 | dialog |
| 132 / 133 | alert |
| 134 / 135 | movable-modal |
| 136 / 137 | floating |
| 138 / 139 | side-floating |
| 140 / 141 | drawer |
| 142 / 143 | movable alert |

Gizmo's document frame pieces (`pxm#`, with their roles):

- `6170` "Doc Win Bottom Corners", `6173`/`6174` "Doc Win Left/Right Planter",
  `6177`/`6178` "Doc Win L/R Vine" — the frame edges and corners.
- `6181` "Doc Win Titlebar Left No Close", `6185` "Doc Win Titlebar Right" —
  the title-bar end caps; the middle is a `plut` fill (`-65` → `ppat` 6159).
- `6155` "Doc Win Title Comb" — the comb teeth strip.
- `10043` "Doc Win TB Bar Collapsed" — the collapsed-window bar.

### Menus — `layo` 256–263

256 root-menu background, 257 pull-down background, 258 pop-up, 259
hierarchical, 260 root-menu item, 261 text-menu item, 262 process-menu handle,
263 menu-item divider.

### Controls — `layo` 300–600

300 tab pane, 310/315/320/325/330/335/340/345 tabs, 400 horizontal scrollbar,
405 vertical scrollbar, 410/415 small scrollbars, 420–427 sliders, 430 progress
bar, 440 indeterminate, 500 push button, 501 default push button, 510 checkbox,
515 radio, 520 pop-up button, 525 arrow button, 530/531/532 bevel buttons,
540 little-arrows, 550 disclosure, 600 image well.

### Control frames — `frme` 500–568, 2000–2001

Each `frme` names a control's frame: 547 active push button, 548 pressed, 549
inactive, 550 focus; 551–554 the default push button; 520/521 edit text frame;
530/531 list box frame; 542/543 window header; 544–546 window placard; 555–557
pop-up arrow; 558–568 bevel buttons; 2000/2001 image well.

### Scrollbar parts — `pxm#` and `plut`

The track is a `plut` fill (`-31` horizontal → `ppat` 1030, `-34` vertical →
`ppat` 1033). The thumb end caps are `pxm#` 500/501 (horizontal) and 502/503
(vertical); the thumb middle is a `plut` fill (`-43`/`-46`). The arrow glyphs
are the standalone `pxm#` 321/322 (up/down) and 331/332 (left/right), drawn in
a 16 px box with a 1 px border.

### Window-frame buttons — `pxm#`

200/201 large/small close box, 210/211 collapse, 220/221 zoom, 450/451 grow
boxes, 458–463 small grow boxes.

## The `layo` resource

Six zero bytes, a `u16` coordinate count, 16-byte coordinate entries, a `u16`
item count, then 40-byte items.

- **Coordinate** (16 bytes): `type` (`0` standard, `1` variable, `2`
  centering), `primary_offset`, `primary_ref`, `stip` (constrain/collapse/
  snap), `secondary_offset`, `secondary_ref`. Negative refs are the content
  rectangle's edges: `-1` top, `-2` left, `-3` bottom, `-4` right.
- **Item** (40 bytes): `visible_only_if` and `invisible_if` flag masks,
  `attribute` bits, a rectangle of four coordinate refs, a 4-byte resource
  type, a `s16` id, alignment, and image index.

Attribute bits (`1<<n`): 0 title-bar, 1 click action, 2 close widget, 3 zoom
widget, 5 draggable, 6 grow box, 7 collapse widget, 8 visible, 9 content area,
10 drawer grow, 11 modal grow, 12 right grow, 13 left grow, 29 tab-pane
content.

Visible/invisible flags (`1<<n`): 0 inactive, 1 on/disabled, 2 mixed/submenu,
3 h-zoom/scrolls-down, 4 v-zoom/scrolls-up, 5 pressed, 6–9 indeterminate
progress frames, 10 indeterminate active.

## The `frme` resource

A control frame is a flat list of **frame elements**, each a pair:

- `u16` fill type / part. Small values (0–5) select the frame part; larger
  sentinel values (`0x00fe`/`0x00ff`, `0x7fff`/`0x8000`, `0xc000`/`0xc001`,
  `0xffff`) select edge/alignment/terminator roles. The exact per-value
  meaning is not yet confirmed against Apple's `Appearance.r`.
- `s16` fill id — a negative `plut` id for a flat colour or pattern, or a
  positive `pxm#` id for artwork.

A push button's face (`frme` 547) is a list of `plut` greys (`-73`…`-78` →
white, 238, 221, 187, 136, 119), not a `pxm#` image. That is why the button
face must be read from `frme` + `plut`, not from `layo` 500.

## `plut`, `ppat`, `pxm#`

- **`plut` 387**: a `u16` count then 8-byte entries `(ppat_id, r, g, b)`. A
  `layo`/`frme` item with a negative id `-k` indexes entry `k-1`; `ppat_id`
  0 means the flat colour is the entry's `rgb`.
- **`ppat`**: a 1/2/4/8-bit pixel pattern, decoded by `tools/macpat.py`.
- **`pxm#`**: a multi-image pixel map (8/16/32-bit), decoded by
  `decode_pxm`; image index selects the layer.

## The geometry gap

The theme's `layo` positions every part relative to the *content rectangle*,
so Gizmo's own window is a thick vine frame with a comb title bar and a
12 px scrollbar. ZacOS's compositor and toolkit draw a fixed 6 px frame, a
22 px title row and a 16 px scrollbar, and only swap the artwork. Faithful
rendering therefore needs the compositor's frame/title/scrollbar metrics to be
theme-driven, not just the extractor to be correct.

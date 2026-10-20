# Platinum fonts: measured reference

ZacOS 9 draws all of its text with two original bitmap fonts:

- `assets/fonts/platinum-system-12.pfont` is for menus, window titles and buttons.
- `assets/fonts/platinum-views-9.pfont` is for icon labels, list views and Get Info values.

The letter shapes are drawn from scratch. Only the metrics come from the Mac OS 8 fonts. Those metrics are cap height, x-height, stroke weight and per-letter spacing, and they were measured from the HIG figures. Text therefore takes up the same room as on Mac OS 8, so menus, titles and lists lay out exactly as in the figures.

`tools/measure/glyphs.py` measured the spacing. It splits a line of text in a figure into letters and reports each letter's ink width and the distance to the next letter. `lib/tests/test_text.c` checks the ink width of every measured string.

## System font (Charcoal 12 metrics)

Measured in figure 4-1 (`img-091-125`: menu bar and Edit menu) and figure 4-3 (`img-094-127`: contextual menu).

| | |
|---|---|
| Cap height | 9 (rows −8..0) |
| x-height | 7 (rows −6..0) |
| Ascenders | 9 (b d f h k l t) |
| Descenders | p q 2, g j y 3, Q 1 |
| Stems | 2 px vertical, 1 px horizontal |
| Spacing | Ink + 2 for most letters; r and f + 1; I starts 1 px in; the period's dot starts 1 px in |
| Space | 4 |
| Line box | 16 rows, baseline at row 12 |

These are the measured advances (pen movement per letter). Unlisted letters follow the same pattern.

| Advance | Letters |
|---|---|
| 4 | i l . |
| 5 | I |
| 6 | f r t |
| 7 | E F L S c s |
| 8 | A C D G H O P Q U a b d e g h k n o p u y |
| 12 | M m w |

Checked strings, by ink width: "File" 21, "Edit" 23, "Show Clipboard" 99, "Select All" 58, "Preferences..." 91, "Copy" 30, "Make Alias" 69, "Get Printer Configuration" 164, "Set as Default Printer" 141.

## Views font (Geneva 9 metrics)

Measured in figure 2-24 (`img-038-058`: Finder list view).

| | |
|---|---|
| Cap height | 8 (rows −7..0) |
| x-height | 6 (rows −5..0) |
| Descenders | 2 |
| Strokes | 1 px |
| Spacing | 1 px after each letter. b h k m n p r start 1 px in. i and l are a stem in their second column with a flag to its left, then 2 px of space. 9 has 2 px after it. |
| Space | 3 |
| Line box | 15 rows, baseline at row 11 |

Checked strings: "External" 39, "Internal" 37, "Promotion" 49, "Vacation" 39, "Name" 24, "2 items, 494 MB available" 129.

## Italic

Alias names use an italic that is synthesised, as QuickDraw did. Each row shifts right by one pixel for every two rows above the baseline, and left below it. The advance stays the same.

## TrueType copies, for programs

`tools/fonts/build_ttf.py` builds the same fonts as TrueType, `assets/fonts/ZacOS9System-Regular.ttf`, `ZacOS9Views-Regular.ttf` and `ZacOS9Views-Italic.ttf` (the synthesised italic above), committed and installed to `/usr/share/fonts/truetype/zacos9/`. Each pixel is a square of the outline, 100 units on a side, in an em of 100 units per pixel of the design size (1200 for System at 12 px, 900 for Views at 9 px); the ascent is the rows above the baseline row and the descent those below, so the line box is the bitmap's (16 and 15 px). Unhinted (`share/fontconfig/zacos9-fonts.conf` turns hinting off for them), every edge falls on a pixel boundary at the design size and Pango's rendering matches the bitmaps pixel for pixel; at other sizes they scale as pixel fonts do. The family names are `ZacOS 9 System` and `ZacOS 9 Views`; the Fonts folder lists them (`fontMacFamilies()`), and programs in a ZacOS session see them. Test `fonts-ttf-current` fails when the `.ttf` files are older than the `.pfont` sources (run the script again); it needs python3-fonttools.

## Coverage and fallback

Both fonts cover:
- ASCII;
- typographic quotes, dashes, ellipsis and bullet;
- ° © ® ™;
- ⌫, for menu shortcuts.

Accented Latin letters are composed by `tools/fonts/build_fonts.py` from the base letter and the font's accent marks (grave, acute, circumflex, tilde, diaeresis, ring, cedilla). Any other character falls back to DejaVu Sans, unantialiased, on the same baseline.

## Editing

Edit the `.pfont` files, then run `python3 tools/fonts/build_fonts.py`. That regenerates `lib/fonts_data.h`, and `--specimen out.png` also renders a 3× specimen. The `fonts-current` test fails if `lib/fonts_data.h` is out of date.

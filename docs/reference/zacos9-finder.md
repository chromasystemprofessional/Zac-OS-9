# Platinum scroll bars and Finder list view: measured reference

Source: Apple, *Mac OS 8 Human Interface Guidelines* (9/2/97), chapter 2.

| Figure | Image | Shows |
|---|---|---|
| 2-24 Disclosure triangles used in Finder list view | `img-038-058` 560×175 | two real Finder windows in list view (lavender accent): item-count header, column headers, rows, triangles, small icons, vertical and horizontal scroll bars |
| 2-26 A horizontal scroll bar | `img-040-060` 140×19 | active scroll bar with a thumb (green accent) |

Grays use the usual notation: a hex digit `n` means `#nnnnnn`.

## Accent color ramps

Every accent color has a ramp of shades. Scroll thumbs use the light end and
menu highlights use the dark end.

| Role | Lavender (default) | Green (fig 2-26) |
|---|---|---|
| grip highlight | `#EEEEEE` | `#CCFFCC` |
| light (thumb edges, grip ridges) | `#CCCCFF` | `#66FF99` |
| thumb body | `#9999FF` | `#33CC66` |
| dark (thumb edges); menu highlight top row | `#6666CC` | `#339966` |
| grip shadow; menu highlight body | `#333399` | `#006633` |
| menu highlight bottom row | `#000088` | *unknown* |

## Scroll bar (horizontal; vertical is the same rotated)

The bar is 16 px across its short axis plus the shared black border lines.
The figure 2-26 rows are listed below, with `y=0` and `y=18` being the black borders.

**Arrow box** (14 px plus a black separator), left end:

- `x=0` is black. Column 1 is white for rows 1..16 and `#DDD` at row 17.
- Row 1 is white for `x=1..13`, with `#DDD` at x=14.
- The fill is `#DDD`, the right column (x=14, rows 2..16) is `#AAA`, and the
  bottom row (row 17, x=2..14) is `#AAA`.
- `x=15` is a black separator.
- The arrow is a solid black triangle, 4 wide and 9 tall, pointing outwards.
  The left arrow occupies x=6..9 and rows 5..13, with its tip at x=6, row 9.
- *Disabled:* the arrow is `#888` and there is no thumb (figure 2-24).

**Track** (recessed). Rows from the top border down:

| Row | Color |
|---|---|
| 1 | `#777` |
| 2–3 | `#888` |
| 4–14 | `#AAA` |
| 15–16 | `#BBB` |
| 17 | `#CCC` |

- Where the track starts after an arrow, and again after the thumb, there is a
  shadow column of `#777` followed by a column of `#888` in rows 2..14.
- When there is nothing to scroll, the track is flat `#EEE` (figure 2-24).

**Thumb** (accent):

- Black lines at both ends.
- Interior row 1: a white corner pixel, then *light*, then *dark* in the last column.
- Rows 2..16: *light* in the first column, *body* in the middle, *dark* in the last column.
- Row 17: *body* in the first column, then *dark*.
- Grip: four ridges, 2 px apart, centred. Each is a *light* column next to a
  *grip shadow* column, over rows 6..12. Row 5 caps the light columns with
  *grip highlight*; row 13 extends the shadow columns.

## Finder window, list view (figure 2-24)

These sit inside the standard window frame (`zacos9-window.md`).

- **Item-count header** ("2 items, 494 MB available"):
  - Starts below the content well's top black line with a white row, then a
    `#DDD` fill and a `#AAA` bottom shade line.
  - The text is centred and black, in the *views font* (cap height 8, smaller
    than the system font). The header is 21 px tall including its shade line.
- **Column headers:**
  - Below the item-count header, starting with a `#111` line.
  - The sorted column is filled `#888` with a `#555` top highlight; unsorted
    columns are `#DDD`.
  - The text ("Name", "Date…") is black in the views font.
  - The bottom has a `#777`/`#AAA` line pair, then `#222`/`#444`.
- **Rows:** a 19 px pitch, with a white separator line between rows. The sorted
  column's body is `#DDD`; other columns are `#EEE`.
- **Disclosure triangle:** a lavender-shaded triangle about 6 px wide and 12 px
  tall at the row's left. Pointing right means collapsed; pointing down means expanded.
- **Small icon:** 16×16, then the name in the views font. Folders are tinted lavender.
- **Scroll bars** as above. The resize box sits where they meet.

*Still to measure:*
- icon view (32×32 icons, label highlight);
- selected-row look;
- exact column offsets;
- the views font's metrics (they will come from our own font).

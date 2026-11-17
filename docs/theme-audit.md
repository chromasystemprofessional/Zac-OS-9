# Theme rendering audit

How to check every part of an imported Mac OS 8/9 Appearance theme against
the Appearance Manager documentation and the user's own theme files, and
what is still not rendered.

This is a working checklist, not a claim that every control has been
measured. The extractor keeps the raw resources it cannot decode yet
(`theme/`, `icons/`, `cursors/`), so a later pass can decode them without
asking the user to find the theme again.

## Sources of truth

- **`docs/theme-format.md`**: the resource formats and canonical IDs we
  have checked against real themes: `layo`, `frme`, `plut`, `ppat`,
  `pxm#`, `scen`, `wctb`, `clr#`, `thme`/`pltn`, `cicn`/`wnd#`.
- **Appearance Manager Reference** (Apple): `ThemeSoundKind`,
  `kThemeMetric*`, control and window part constants. Use the public PDF
  from Apple's documentation archive; do not add Apple artwork or templates
  to the repository.
- **`docs/themes.md`**: what a ZacOS theme bundle contains and how each
  file is used.
- **The user's own theme files**: the resource forks under Theme Files are
  the ground truth for IDs, dimensions, palettes and visibility masks.
  Never ship them.

## 1. Inventory the raw resources

For each source theme, list every resource type and ID. This finds data
that an extractor pass ignored, including metadata that is not rendering
data:

```sh
python3 - <<'PY'
import collections, importlib.util, pathlib
spec = importlib.util.spec_from_file_location('ex', 'tools/extract-macos9-theme.py')
ex = importlib.util.module_from_spec(spec); spec.loader.exec_module(ex)
root = pathlib.Path.home() / '.config/zacos9/themes'
for bundle in sorted(root.iterdir()):
    conf = bundle / 'theme.conf'
    if not conf.is_file():
        continue
    source = next((line[7:] for line in conf.read_text(errors='replace').splitlines()
                   if line.startswith('source=')), '')
    if not source or not pathlib.Path(source).exists():
        continue
    fork = ex._fork_from_path(source)
    counts = collections.Counter(t.decode('latin1') for t, _rid, _body
                                 in ex.parse_resource_fork(fork))
    print(bundle.name, ' '.join(f'{t}:{n}' for t, n in sorted(counts.items())))
PY
```

Then compare each type with `bucket_for()` in the extractor:

| Class | Examples | Where it goes |
|---|---|---|
| Window/title art | `layo`, `frme`, `pxm#`, `plut`, `ppat` | `chrome/`, raw `theme/` |
| Colours | `clut`, `pltt`, `clr#`, `wctb` | `theme.conf`, raw `theme/` |
| Theme description | `scen`, `thme`, `tinf` | `theme.conf`, raw `theme/` |
| Icons | `icns`, `icl8`, `ics8`, `ICN#`, `icm#` | `icons/`, raw |
| Cursors | `crsr`, `CURS`, `acur` | `cursors/`, raw |
| Sounds | `snd `, `sdid`, `snd#` | `sounds/`, `soundset/`, cache |
| Patterns/pictures | `ppat`, `PAT `, `PAT#`, `PICT` | `patterns/`, `wallpapers/` |
| Fonts | `sfnt`, `FOND`, `nfnt` | `fonts/`, raw |
| Standard app metadata | `BNDL`, `FREF`, `DITL`, `DLOG`, `vers` | intentionally not kept |

Anything in the first eight classes that is not decoded should still be
kept raw. The unhandled set today is: `tdat`, `tic#`, `tinf`, `dftb`,
`dlgx`, `dctb`, `ictb`, `ftag`, `tvar`, `tthm`, `ntek`, `jthm`, `pthm`,
`fthm`, `zthm`, `sys7`, `TMDA`, `ckid`, `Wpos`, `Mngl`, `MSco`, `hiro`,
`iSNP`, `acur` and `icm#`/`icm4`/`icm8`.

`tdat` is the most important of these: it is theme text/metric data, so
menu, control and disabled text colours can remain Platinum instead of
following the theme. `dftb`/`dlgx` are default-button and dialog extension
data; `icm*` and `acur` are mono icons and animated cursors. The rest are
creator/theme markers and should be preserved but may not affect drawing.

## 2. Map each visible part to its resource and metric

Use this matrix for every theme. “Source” is the resource from
`docs/theme-format.md`; “Renderer” is the ZacOS code that consumes it.

| Part | Source | Renderer | Status |
|---|---|---|---|
| Document frame edges | `layo` 128/129, `pxm#`/`plut`/`ppat` rails | `compositor/src/decor.c` | Edges, left/right/bottom rails, caps; top is the title bar |
| Document frame corners/caps | `pxm#` corner groups | `decor.c` | Gizmo caps; generic corners partial |
| Title bar | `layo` 128/129 title items | `decor.c` | Active/inactive, left cluster, spans; fixed 22 px row |
| Close/collapse/zoom/grow | `pxm#` 200–221, 450–463 | `decor.c` | At theme tile size; dialog variants |
| Dialog/alert frame | `layo` 130–133 | `decor.c` | Uses the document/movable-modal drawing |
| Movable modal | `layo` 134/135 | `decor.c` | Separate title pieces and boxes |
| Floating/side-floating/drawer | `layo` 136–141 | `decor.c` | Not theme-specific |
| Menu bar | `layo` 256, `menu-bar-image` | `lib/menudraw.c` | Background art and palette; menu title plates not |
| Pull-down/pop-up menus | `layo` 257–259, `menu-background` | `menudraw.c` | Face, border, highlight, separators; marks/indents fixed |
| Menu item/check/hierarchy | `layo` 260–263, `pxm#` | `menudraw.c` | Text, 9 px mark, fixed indent/metrics |
| Push/default/cancel/bevel buttons | `layo` 500/501/530–532, `frme` 547–568 | `lib/widgets.c`, Qt style | Push/default face and ring; bevels not |
| Checkbox/radio | `layo` 510/515, `frme` 510–515 | `widgets.c`, Qt style | Bitmaps from theme; fixed 12/13 px |
| Pop-up button | `layo` 520, `frme` 555–557 | `widgets.c` | Drawn by Qt; fixed metrics |
| Tabs | `layo` 300–345 | none | Platinum/fixed |
| Scrollbars | `layo` 400–415, `pxm#` 500–503, 321/322/331/332 | `widgets.c`, Qt style | Track, thumb, arrows, smart scrolling; fixed 16 px |
| Sliders | `layo` 420–427 | none | Platinum/fixed |
| Progress | `layo` 430/440, `frme` | `widgets.c` | Fill art; fixed 10×12 |
| Disclosure/little arrows | `layo` 540/550 | `widgets.c` | Fixed Platinum geometry |
| Image well | `layo` 600, `frme` 2000/2001 | none | Platinum/fixed |
| Group boxes/list headers | `frme` 542–546, 530–531 | none | Platinum/fixed |
| Edit text/focus ring | `frme` 520/521, focus metrics | Qt style | Fixed Fusion/Platinum |
| Text colours | `clr#` 384, `tdat` | `decor.c`, Qt style | Title text only; `tdat` not read |
| Standard icons | `icns`/`icl8`/`ics8`/`ics4`/`ICN#` | `items.cpp` | 18 kinds at 16/32 px |
| Mono icons | `icm#`/`icm4`/`icm8` | none | Preserved raw |
| Cursors | `crsr`/`CURS`/`acur` | `cursors/`, compositor | Static cursors; `acur` not used |
| Fonts | `sfnt`, `FOND`, `nfnt` | `fonts/` | TrueType only; bitmap fonts not |
| Sounds | `snd `, `sdid`, `snd#` | `shell/panels/zacos9-soundthemes` | v0/v1, documented events; variants partial |
| Desktop pattern | `scen` `patt`, `PAT `, `PAT#` | `zacos9-patterns` | User/theme tiles |
| Desktop picture | `scen` `dpnm`, `PICT` | `zacos9-patterns --wallpaper` | Files from Desktop Pictures; theme previews skipped |
| Alert sounds | `snd ` resources | `lib/settings.c` | Theme sounds and system sounds |
| Boot/menu icon | `icns`/`ics8` –16386 | `logo.c`, `boot` | Menu icon and boot art |

## 3. Metrics audit

The official `kThemeMetric*` constants are the checklist for geometry. The
most visible ones are:

- `kThemeMetricTitleBarControlsHeight`
- `kThemeMetricScrollBarWidth`, `kThemeMetricSmallScrollBarWidth`,
  `kThemeMetricScrollBarOverlap`
- `kThemeMetricCheckBoxHeight`/`Width`,
  `kThemeMetricRadioButtonHeight`/`Width`
- `kThemeMetricPushButtonHeight`, `kThemeMetricSmallPushButtonHeight`
- `kThemeMetricLargeTabHeight`/`SmallTabHeight` and cap widths
- `kThemeMetricPopupButtonHeight`, `kThemeMetricPullDownHeight`
- `kThemeMetricLargeProgressBarThickness`,
  `kThemeMetricNormalProgressBarThickness`
- `kThemeMetricHSliderHeight`, `kThemeMetricVSliderWidth`
- `kThemeMetricDisclosureTriangleHeight`/`Width`,
  `kThemeMetricLittleArrowsHeight`/`Width`
- `kThemeMetricListHeaderHeight`, `kThemeMetricImageWellThickness`
- `kThemeMetricMenuMarkColumnWidth`, `MenuIndentWidth`,
  `MenuTextLeadingEdgeMargin`, `MenuTextTrailingEdgeMargin`

For each, derive the theme's value from its `layo` coordinates and `frme`
header/insets, then compare with the constant used by the renderer. Today
the compositor and Qt style still use fixed Platinum geometry for most of
these; only the document frame edges and the title bar's art are
theme-driven. That is the largest remaining gap, and is the “geometry gap”
described at the end of `docs/theme-format.md`.

## 4. Rendering probes

Use synthetic resources (no Apple art) so the test can ship:

1. Build a fixture with `layo 128/129` (document), `130/131` (dialog),
   `134/135` (movable modal), `256` (menu bar), `257` (menu), `500` (push
   button), `510` (checkbox), `515` (radio), `400/405` (scrollbars), and
   `pxm#` rails and buttons.
2. Render each state to PNG at 1× and 2×: active, inactive, collapsed,
   pressed, focused, disabled.
3. Check invariants:
   - every frame edge is opaque, except deliberate rounded-corner
     transparency;
   - title-bar art is a fill, not a repeated end cap;
   - menu text contrasts with its face;
   - control face/bevel rings match `frme` + `plut`;
   - scrollbar track/thumb/arrows match their `pxm#`/`plut` sources;
   - the same output is produced at 1× and 2×.

`tests/tools/test_extract_macos9.py`, `tests/panels/test_customthemes.cpp`
and the compositor decor tests already cover pieces of this. A complete
theme fixture would add a single render harness that produces a contact
sheet for every installed theme; run it in a disposable session (see
`tests/disksetup/stage.py`) so it never touches the user's desktop.

## 5. What is still left behind

- Theme metrics are mostly fixed: title row 22 px, frame 6 px, scrollbars
  16 px, buttons/checkboxes/radios/progress/tabs fixed sizes.
- `layo` window types 130–143 are not separately rendered; floating,
  drawer and alert frames reuse the document/movable-modal art.
- Controls without theme geometry: tabs, sliders, pop-up buttons, bevel
  buttons, disclosure triangles, little arrows, image wells, list headers,
  group boxes and focus rings.
- Menus: title plates (Hi-Tech’s raised menu titles), mark/indent metrics
  and item highlight art beyond the generic accent bar.
- Text colours: only `clr#` title entries are read; `tdat` and the other
  `clr#` entries are not applied to menus/controls.
- Fonts: TrueType only; `FOND`/`nfnt` bitmap fonts.
- Icons/cursors: `icm#`/`icm4`/`icm8` and `acur` are preserved raw but not
  drawn.
- `thme`/`pltn` fields, `tinf`, `tic#`, `dftb`, `dlgx`, `dctb`, `ictb` and
  creator markers are preserved raw but not interpreted.
- `PICT` theme previews and the theme’s credit splash are not drawn.
- `scen` tags not used: `epic` previews are skipped, `smsk` and some
  creator variants have no rendering path.
- `pxm#` masks/layers and `frme` large/bevel frames are only partly
  decoded.

Keeping the raw resources is deliberate: when a later pass learns to read
one, it can rebuild from the bundle already in Theme Files. The audit
should update this list and the compatibility table in `docs/themes.md`
when a gap is closed.

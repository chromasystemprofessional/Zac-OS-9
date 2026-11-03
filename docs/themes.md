# Appearance themes and sound sets

Appearance follows the classic tabbed control-panel model. **Themes** applies
named combinations of accent, highlight, desktop background, alert sound and
interface sound set. **Save Current** saves the current combination under a
new name. **Platinum** restores the original default appearance and disables
interface sounds.

Presets are stored in **System Folder > Appearance > Theme Files**, normally
`~/.local/share/zacos9/appearance/Theme Files`. Each dropped theme keeps its
preset beside itself (`Theme Files/<name>/*.json`); presets from before live
in the root until the desktop files them over. A custom preset is a JSON file:

```json
{
  "version": 1,
  "name": "My Desktop",
  "settings": {
    "accent": "",
    "highlight": "FFCC66",
    "background": "pattern",
    "pattern": "",
    "sound-theme": "original",
    "font-family": ""
  }
}
```

Empty accent/pattern IDs mean the built-in defaults. `font-family` names the
program typeface, or is empty for the default; it must be installed when the
preset is applied. Background can be
`pattern` or `wallpaper`; wallpaper presets can supply `wallpaper` (a catalog
ID) and `wallpaper-mode` (`fit`, `fill`, `stretch`, `center`). Save Current
records the actual IDs, so user assets must remain available when a preset is
applied. Presets are settings combinations, not arbitrary window/control skins.
Full classic Mac appearance skin resources are explicitly reported as
unsupported; no executable resources are loaded or run.

## Mac OS 9 themes

ZacOS never ships Apple's artwork, but the **Theme Installer**
(`zacos9-themeinstaller`) builds a theme from your own Mac OS 9 install. It
reads the Appearance theme's resource fork and produces a **theme bundle** in
`~/.config/zacos9/themes/<name>`:

- `theme.conf` — the accent, highlight, desktop pattern and the window-chrome
  colours (`title-base`, `title-stripe-light`, `title-stripe-dark`,
  `frame-light`, `frame-dark`, `frame-border`, `frame-face`), each an RRGGBB
  hex value. The chrome colours come from the file's own gray colour table
  when it has a real one (Apple themes do); Kaleidoscope schemes store no
  flat chrome colours — their bars are composed artwork, which ZacOS does
  not skin — so theirs are approximated from the scheme's own desktop
  pattern tile (lightened for readability), distinct per theme. Pass
  `--title-base` and friends (or `--accent`/`--highlight`/`--pattern`) to set
  them by hand instead. Themes with lined-up window art (see below) also
  get `bar-middle`, `bar-middle-inactive` and `button-<name>[-pressed]`
  keys naming their `chrome/` tiles; the compositor tiles those over the
  flat colours. The extractor also decodes the theme's `scen` collection (a
  flattened Appearance Manager collection) and writes its font and behaviour
  settings: `system-font`, `small-system-font`, `views-font`,
  `views-font-size`, `double-click-collapse`, `smooth-fonts`,
  `smooth-font-min-size`, `desktop-picture`, `pattern-name`,
  `highlight-name`, `variant`, `scroll-bar-arrows` (single or
  double-lower-right) and `scroll-bar-thumb` (normal or proportional). The raw `scen` resources are kept under
  `theme/` for later decoding of scroll-bar geometry.
- `menu-icon.png` — 20×15, replaces the Zac menu icon: Mac OS 9's Apple
  menu icon when the user's System file supplied one, else the theme
  file's own icon.
- `apple-menu.png` — 16×16, the System file's menu-bar Apple icon
  (`ics8`/`ics4` id -16386), decoded when the user's own Mac OS 9 System
  file is present (single-file mode reads it from the System file above
  the theme file, else `~/Classic`); kept separate so a theme can use it
  explicitly. The menubar shows it centred in the logo slot while the theme
  is active, falling back to `menu-icon.png`.
- `file-icon.png` — 32x32, the theme file's own Finder icon (family 1024)
  where it has one; the Sniffer stamps it onto the dropped file.
- `index.theme` and `cursors/` — the pointers as Xcursor themes, each
  inheriting the rest from ZacOS9. The arrow family (`default`,
  `copy`, `alias`, `context-menu`) comes from the theme file's colour cursors
  (`crsr` 128-131). The busy, resize, move, pointing and grabbing hands,
  crosshair, progress and not-allowed pointers come from the System file
  (`--system`, or `~/Classic/System/System` in the Theme Installer), written
  at 1x, 2x and 3x into a set named after the bundle folder. The decoded
  `cursors/<name>-16.png` tiles plus `cursors/cursors.json` (hot spots
  included) also build a `cursors/Macintosh/` set — the same pointers with
  hand-placed art (like the ROM-only arrow) folded in — which the compositor
  prefers while the theme is active. The compositor reads the bundle at
  start-up, so changes apply the next time the desktop starts.
- `boot/happy-zac-1x..6x.png` and `boot/zacos-logo-1x..6x.png` — optional
  hand-placed overrides for the boot and Welcome artwork. The extractor
  writes none: the theme file's own icon is its Finder icon, not startup
  art. When a Mac OS 9 theme is active without them, Happy Zac and the
  Welcome logo stay.
- `wallpapers/desktop-wallpaper.jpg|.png` — the desktop picture the theme names
  (`scen` `dpnm`), found under `Appearance/Desktop Pictures` in your Mac OS 9
  System Folder (a JPEG data fork is copied as-is, otherwise the `.rsrc`
  companion's PICT is decoded). The theme file's own PICTs are its credit
  splash (id 128) and the Appearance panel's preview thumbnails (the `scen`
  `epic` ids), never wallpapers, so they are skipped. Only screen-sized
  PICTs (128 px or more on each side) become wallpapers; smaller
  icon-sized pictures are left out. The installer accepts PNG, JPEG, GIF,
  TIFF and BMP pictures and converts them to PNG, because the desktop
  loads wallpapers as PNG.
- `patterns/desktop-pattern.png` — the desktop pattern, stored inside the
  `scen` collection (`patt` tag), plus the monochrome `PAT` tiles as
  `pattern-N.png`. The theme file's colour `ppat` resources are scroll-bar,
  window-border and button skins as well as Kaleidoscope tiles, so both
  `colour-pattern-N.png` files and numbered `pattern-N.png` tiles are kept;
  the installer only files the desktop pattern and the mono tiles as desktop
  art.
- `fonts/views.pfont` and `fonts/system.pfont` — the theme's fonts from your
  Mac OS 9 System file and Fonts folder: the views font at the theme's views
  size, and the system font at 12 points. A family's bitmap strike is used
  when it has one for that size (NFNT); otherwise its TrueType font is
  rasterised at that size (Charcoal, for example). Bitmap strikes keep their
  offset/width-table advances and side bearings, including the space width.
  Alongside them, `fonts/*.ttf` holds the decoded TrueType faces and
  `fonts/theme-*.pfont` the window-chrome bitmaps rasterized from them: the
  theme's system font at text sizes (`theme-system-12/15/18/21`, covering 1x
  and the fractional scales) and its views font at list sizes
  (`theme-views-11/14/17/19`). While the theme is active the menus, titles
  and lists use these instead of ZacOS's own bitmaps; faces or sizes the
  theme lacks fall back per glyph, so partial coverage stays readable.
  ZacOS loads them at 1x and keeps its own fonts at 1.25x, 1.5x and 1.75x.
  Families not on your Mac (Tecton, Osaka...) keep ZacOS's fonts. TrueType
  glyphs are rasterised without Mac hinting, one bit per pixel.
- `chrome/*.png` — the window-frame skins, from the theme's named frame
  patterns (`document window top/bottom/left/right`, `Modal Top/...`,
  `document window tab center`, each with an `inactive` variant where the
  theme ships one). The compositor tiles them over title bars and borders;
  the menu bar, dropdown menus and the selected item use the `menu
  background` / `menu highlight` skins. Scrollbars tile the `scroll-track-*`
  and three-part `scroll-thumb-*` skins (caps with the stretch between,
  stretch alone when short); the arrows and disabled state stay procedural.
  Buttons tile the `button` face inside their rounded border
  (`-inactive`/`-pressed` variants when the theme ships them). The
  determinate progress fill uses `progress-fill`. Kaleidoscope themes add
  their own set beside it, decoded from the frame layout: `bar-middle`
  (`bar-left`, `bar-right`, `-inactive` variants) tiles the title bar
  stretched edge to edge, and `button-close/zoom/grow` (plus `-pressed`
  variants) draw over the title-box slots, leaving hit-testing and geometry
  untouched. Movable-modal dialogs get their own set from the frame layout's
  dialog records (`layo` 134/135). Themes whose buttons ride with the title
  group instead get a two-row bar with a full-width blue band
  (`bar-span`/`bar-span-inactive`). Each tile ships as a PNG (for the
  catalog sheets) and a flat `.argb` dump (u32le width and height, then
  straight RGBA) that the compositor reads directly. Close/zoom/collapse
  boxes, title text and hit-testing stay ZacOS's. Missing files fall back
  to the Platinum drawing.
- `sounds/` — alert sounds. `system-sound-N.wav` are the System file's own
  `snd` resources, decoded from sampled sounds only; synthesized and compressed
  sounds are skipped. Theme-file sounds sit beside them. The installer never
  picks a `system-` sound as the theme's default alert.
- `icons/<kind>-32.png` and `icons/<kind>-16.png` — the desktop's icons,
  decoded at both sizes against the user's System palette: a theme
  file's own art wins where it supplies a family (Kaleidoscope schemes
  draw against the palette they display through), and the System and
  Finder files fill the kinds it lacks: folder, the System Folder,
  trash empty and full, document, hard disk, network disk, disk
  image and the caution sign. The Sniffer and
  the alert box use them while the theme is active; every other icon
  keeps ZacOS's own art. Custom folders (System Folder, Applications,
  Home, Control Panels and the System Folder's emblem folders) keep
  their built-in center symbols, drawn over the theme's folder shape.
  8-bit Mac icons index the ROM's default palette,
  which survives in no resource fork: entries checked pixel-against-pixel
  with a running Mac OS 9 are corrected in the extractor
  (`CALIBRATED_SYSTEM_COLORS` in `tools/macicons.py`). The raw resources
  stay under `icons/` (`<type>_<id>.bin`) for reference.
- `cursors/<name>-16.png` and `cursors/cursors.json` — the pointer
  shapes decoded from the user's own System and Finder files (I-beam,
  crosshair, move, watch, two hands), 16x16 with hot spots recorded in
  the JSON (see the `cursors/` Xcursor sets above for how they are used).
  Hand-placed files (`arrow-16.png` plus a `cursors.json` entry) are picked
  up on the next import, which regenerates the theme without dropping them.
- `wallpapers/`, `patterns/` and `sounds/` — decoded desktop pictures, pattern
  tiles and alert sounds, copied into the Appearance folders when the theme is
  installed. Only screen-sized PICTs (128 px or more on each side) become
  wallpapers; smaller icon-sized pictures are left out.

The extractor (`tools/extract-macos9-theme.py`) decodes the classic icon,
PICT, pattern and sound resources (`macpict.py`, `macpat.py`, `macsnd.py`); `--list-cluts` prints the colour tables and
`--auto-colours` (or `--accent`/`--highlight`/`--pattern`) set the chrome
colours. Window geometry (title-bar height, close/zoom/collapse positions,
margins) is never changed — only colours and artwork.

To install: put your Mac OS 9 System Folder in `~/Classic` (Old Dog), pick the
look in Mac OS 9's Appearance control panel, then copy the theme file out of
Mac OS 9 (Appearance ▸ Theme Files to your home folder, which materialises its
resource fork) and drop it into System Folder > Appearance > Theme Files in the
Sniffer. The desktop files it into its own folder (Theme Files/<name>/ holds
the dropped file, its resource fork and the generated preset), builds the
theme from it and applies it. Dropping another copy of an installed theme does
nothing; a changed file imports again. (The import runs `zacos9-themeinstaller`
behind the scenes; its `--file` batch mode does the same from a terminal.)

The Theme Installer finds the System Folder at `~/Classic/System Folder`
(where Old Dog keeps it), at `~/System Folder` (where a copy dragged onto
Old Dog's Unix volume lands) or at `$ZACOS9_CLASSIC_DIR/System Folder`. It
reads the Apple menu icon, the busy pointers, the alert sounds, the desktop
pictures and the fonts from it; without one, the theme still installs but
keeps ZacOS's Apple, pointers, sounds and fonts, and takes no desktop
picture.

When there is none, the installer offers to copy it out of Old Dog's startup
disk: it mounts the disk read-only (`zacos9-mac-disks`, which may ask for an
administrator's password), copies `System Folder` to `~/Classic/System
Folder` keeping every file's resource fork as its `.rsrc` companion, and
goes on with the import. `zacos9-themeinstaller --copy-system-folder` does
the same from a terminal. Old Dog must not be running from that disk while
it is copied. Finder metadata (type and creator) is not carried over: the
copy is for ZacOS to read, not to start up from.

A bundle records the extractor that built it, so when a ZacOS
update learns to decode more, the Appearance panel rebuilds the bundles
from the copies in Theme Files rather than leaving the old, partial ones.
The dropped file wears the theme's own Finder icon (its mini window art) where
one exists; files without one keep the generic document icon. The menu icon,
boot art and colours change at the next menu-bar rebuild and boot. Decoded TrueType faces install to `~/.local/share/fonts/zacos9/` (registered
with fontconfig); the preset's `font-family` (the theme's system font, else its
views font) becomes the programs' typeface, selectable any time under
Appearance > Fonts. The desktop chrome (menus, titles, lists) uses
the `theme-*.pfont` bitmaps rasterized from those same faces.

## Sound sets

**Sound Sets** chooses None, the original ZacOS sounds, or a custom set.
**Interface Volume** is separate from Alert Volume. **Preview** plays an example
from the selected set. Sounds play through the system's default output and its
mute state. The existing alert-sound choice and volume-adjustment feedback are
unchanged. ZacOS control buttons, checkboxes, menu commands, Sniffer windows and
Trash operations emit discrete events; third-party application controls are
not intercepted. Closely spaced events are rate-limited.
Only one interface event plays at a time, preventing long custom sounds from
overlapping. Explicit previews and alerts remain independent.
Window collapse and expansion have distinct events, including title-bar
double-click and the collapse button. Window movement starts a repeating
`window-drag` sample on the first actual movement, not merely a title-bar click.
Releasing the grab stops it and optionally plays `window-drag-end`; Escape,
window closure, fullscreen transitions and session shutdown cancel it without
an end sound. These compositor events also work for other applications' windows.

Place user-created or freely licensed sets in **System Folder > Appearance >
Sound Themes**, normally `~/.local/share/zacos9/appearance/Sound Themes`.
For a WAV set, create a subfolder containing `theme.json` and the referenced
files:

```json
{
  "version": 1,
  "name": "My Sound Set",
  "events": {
    "button-click": "click.wav",
    "checkbox-toggle": "toggle.wav",
    "menu-open": "open.wav",
    "menu-command": "choose.wav",
    "window-open": "window.wav",
    "window-close": "close.wav",
    "window-collapse": "collapse.wav",
    "window-expand": "expand.wav",
    "window-drag": "drag.wav",
    "window-drag-end": "drop.wav",
    "trash-move": "trash.wav",
    "trash-empty": "empty.wav",
    "startup": "startup.wav",
    "shutdown": "shutdown.wav"
  }
}
```

Files are PCM WAV audio; missing events stay silent. Relative paths must stay
inside the set; symbolic links and unsafe paths are rejected. Catalog discovery
checks for additions, replacements and removals every two seconds. Imported
audio is cached under the user's XDG cache directory; source files are unchanged.
The optional `startup` and `shutdown` events play when the desktop starts and
when the user logs out after applications close.
Missing selected sets become silent while retaining their selection ID.
Malformed or unsupported sets report an error in Appearance and the session log.

Classic resource-fork sound sets can also be placed in Sound Themes. Preserve
their resource forks, including companion `._` AppleDouble files produced by
StuffIt extraction. Supported PCM resources are decoded to cached WAVs;
unsupported encodings are reported, not played as noise. No Apple sounds or
third-party archive artwork is bundled.
Sniffer preserves an individual classic file's hidden AppleDouble companion
when it is moved or copied, including renamed duplicates.
You can also copy the extracted `Sounds` folder into Sound Themes: one level
of contained classic sets is scanned. Deeper nesting is reported rather than
recursively traversed. Resource-only StuffIt entries receive visible empty
data files so they can be selected normally in Sniffer.

Classic discrete events supported are button press (`btnp`), checkbox press
(`chkp`), menu open (`mnuo`), menu selection (`mnus`), window open (`wopn`),
window close (`wcls`), collapse (`wcol`), expansion (`wexp`) and Trash flush
(`ftrs`). Window movement (`wmov`) imports the record's drag slot as
`window-drag`, which repeats throughout the grab, and its release slot as
`window-drag-end`. Classic sets keep these in the second and fourth `snd#`
slots (the first is normally empty) and often share them with other drag
records, so a shared sound label does not block them. These identifiers follow
the public Carbon HIToolbox Appearance Manager interface. Playback repeats the
complete decoded sample; the idle-drag variant, embedded loop regions and
random sound sequences are not supported.

Samples may be 8- or 16-bit PCM, μ-law, IMA 4:1 or `twos`, at 1–96 kHz. MACE
is not decoded and is reported. Leading null sound commands are skipped, and
an empty sound resource silences its event without an error. There is no
verified classic mapping for `trash-move`; WAV themes can supply it explicitly.
The importer accepts up to 256 custom sets with a 256 MiB decoded-audio budget;
individual samples are limited to 30 seconds and 8 MiB. Limit violations are
reported explicitly.

## Tests

Tests use original WAVs and synthetic classic resources. They cover resource
parsing and sound event mapping, safe file handling, catalog changes, preset
validation, selection persistence, muted interface volume, preview and playback
argument routing. The user-supplied compatibility archive is ignored by Git and
excluded from package builds.

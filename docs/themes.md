# Appearance themes and sound sets

Appearance follows the classic tabbed control-panel model. **Themes** applies
named combinations of accent, highlight, desktop background, alert sound and
interface sound set. **Save Current** saves the current combination under a
new name. **Platinum** restores the original default appearance and disables
interface sounds.

Presets are stored in **System Folder > Appearance > Themes**, normally
`~/.local/share/zacos9/appearance/Themes`. A custom preset is a JSON file:

```json
{
  "version": 1,
  "name": "My Desktop",
  "settings": {
    "accent": "",
    "highlight": "FFCC66",
    "background": "pattern",
    "pattern": "",
    "sound-theme": "original"
  }
}
```

Empty accent/pattern IDs mean the built-in defaults. Background can be
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
  hex value. The extractor also decodes the theme's `scen` collection (a
  flattened Appearance Manager collection) and writes its font and behaviour
  settings: `system-font`, `small-system-font`, `views-font`,
  `views-font-size`, `double-click-collapse`, `smooth-fonts`,
  `smooth-font-min-size`, `desktop-picture`, `pattern-name`,
  `highlight-name`, `variant`, `scroll-bar-arrows` (single or
  double-lower-right) and `scroll-bar-thumb` (normal or proportional). The raw `scen` resources are kept under
  `theme/` for later decoding of scroll-bar geometry.
- `menu-icon.png` — 20×15, replaces the Zac menu icon.
- `apple-menu.png` — 16×16, the System file's menu-bar Apple icon
  (`ics8`/`ics4` id -16386), decoded when the user's own Mac OS 9 System
  file is present; kept separate so a theme can use it explicitly.
- `boot/happy-zac-1x..6x.png` and `boot/zacos-logo-1x..6x.png` — replace the
  boot and Welcome artwork.
- `wallpapers/`, `patterns/` and `sounds/` — decoded desktop pictures, pattern
  tiles and alert sounds, copied into the Appearance folders when the theme is
  installed.

The extractor (`tools/extract-macos9-theme.py`) decodes the classic icon,
PICT, pattern and sound resources; `--list-cluts` prints the colour tables and
`--auto-colours` (or `--accent`/`--highlight`/`--pattern`) set the chrome
colours. Window geometry (title-bar height, close/zoom/collapse positions,
margins) is never changed — only colours and artwork.

To install: put your Mac OS 9 System Folder in `~/Classic` (Old Dog), pick the
look in Mac OS 9's Appearance control panel, then run **Theme Installer** from
the Zac menu. It writes a preset into Appearance > Themes and applies it. The
menu icon, boot art and colours change at the next menu-bar rebuild and boot.

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

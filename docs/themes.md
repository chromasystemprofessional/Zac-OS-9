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
    "trash-move": "trash.wav",
    "trash-empty": "empty.wav"
  }
}
```

Files are PCM WAV audio; missing events stay silent. Relative paths must stay
inside the set; symbolic links and unsafe paths are rejected. Catalog discovery
checks for additions, replacements and removals every two seconds. Imported
audio is cached under the user's XDG cache directory; source files are unchanged.
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
window close (`wcls`) and Trash flush (`ftrs`). Classic drag-loop sounds,
random/command sequences and compressed samples are not supported. There is
no verified classic mapping for `trash-move`; WAV themes can supply it explicitly.
The importer accepts up to 64 custom sets with a 64 MiB decoded-audio budget;
individual samples are limited to ten seconds and 2 MiB. Limit violations are
reported explicitly.

## Tests

Tests use original WAVs and synthetic classic resources. They cover resource
parsing and sound event mapping, safe file handling, catalog changes, preset
validation, selection persistence, muted interface volume, preview and playback
argument routing. The user-supplied compatibility archive is ignored by Git and
excluded from package builds.

# Project status

Updated: 2026-11-05. Latest release: **1.9.3**
([releases](https://github.com/chromasystemprofessional/Zac-OS-9/releases),
[ISO](https://sourceforge.net/projects/zac-os-9/files/)).

This page is what's true now and what's next. What changed and when is in
`git log`, `debian/changelog` and the release notes; how each part works is in
[`docs/`](docs/) and [ARCHITECTURE.md](ARCHITECTURE.md).

## Where things stand

- **The Mac OS 9 plan is built.** Every work package in
  [`docs/mac-os-9-gap-plan.md`](docs/mac-os-9-gap-plan.md) (milestones 1–9,
  83 packages) had landed by 2026-10-20.
- **Installing next to another system** (Windows, Haiku, another Linux) came
  in 1.0.21, with a partition map in Disk Setup and the installer
  ([Installer](docs/installer.md)). It was tested in virtual machines with
  BIOS and UEFI, next to Haiku and a Windows partition.
- **1.0.22** fixed the Sniffer crashing after the Fonts folder, and a
  tablet's pen landing in the wrong place in Krita.
- **1.1.0** is the restructuring below, with the fixes it turned up (File
  Sharing's password check, the last `waitForFinished()` calls).
- **1.4.0** adds console game support: the Sniffer recognises NES, Super NES,
  PlayStation, Nintendo 64, Sega and Game Boy cartridges and discs (their own
  icons and kind names), and the Game Player extra (in ZacOS Extras) runs them
  through RetroArch, mednafen and mupen64plus.
- **1.5 to 1.7 add Mac OS 9 theming.** The Theme Installer builds a theme from
  your own Mac OS 9 System Folder (Apple menu icon, window chrome, desktop
  picture, alert sounds, fonts), a theme file dropped on the System Folder
  files itself into Appearance ▸ Theme Files, and the Appearance panel wears
  Mac OS 9's six tabs with a gallery of previews. A one-time What's New window
  explains the drop after an update.
- **1.7.2 to 1.7.5 make printing and themes reliable.** A failed document is
  named while the rest still print, queues give a job's reason, network
  printers appear before their first job, one printer over USB and Wi-Fi is one
  row, and a theme dropped on the desktop now copies the System Folder it needs
  so it gets its Apple menu icon.
- **1.7.6 repairs the System Folder copied from Old Dog's disk.** A
  kernel-mounted HFS volume exposes only data forks, so the copy had no
  resource fork and themes showed the theme file's document icon. The copy is
  now read from the image itself, every resource fork kept, and older copies
  are repaired in place; themes rebuild when Appearance opens.

## Being worked on

**The System Folder**, made to work the way Mac OS 7 to 9's did, item by
item: [`docs/system-folder.md`](docs/system-folder.md) has every item, what
ZacOS has for it, and the order. All six items are done.

**Monitors**: each screen's resolution, pixel size and rotation are
applied live (`compositor/src/prefs.c`): rotation is
`display.NAME.rotation` (0, 90, 180 or 270 degrees), the layout uses the
rotated logical size, and `display-rotation` tests it.

**Interface languages**: the gettext catalogs, the language setting and the
text-fit check are in place. Italian is the first catalog: the control panels'
names and titles, the Mouse panel, the menu bar's menus and Hint Bubbles, the
Collar's modules and the everyday applications' names and window titles; the
installer now asks for the installed system's language, translates its own
screens, and the login window and Setup Assistant follow it; the Finder's
alert buttons, core dialogs, Copying Files, View Options, Preferences, Find,
Get Info, the Extensions Manager and the shared disk-plan strings are in too;
every Finder alert and the Help viewer's UI are translated; the viewer
overlays translated help pages on the English tree, the whole Italian help
book and tutorial are in, and Bark Recorder and Neighbourhood are translated
([Languages](docs/i18n.md)). The remaining product-named apps and a native
review are next.

**Japanese (display pilot)**: `lib/text.c` falls back through Fontconfig to
Noto Sans CJK, `wrapText()` breaks Japanese without spaces, and `po/ja.po`
translates the Mouse panel, every control panel's name and title, and the
Mouse desktop entry. Input is not supported:
ZacOS's own text fields have no IME preedit, so the language is not
advertised as usable yet ([Languages](docs/i18n.md)).

**Arabic**: `po/ar.po` is complete at 817 strings, including the Finder,
menu bar, application names, installer and Setup Assistant; `Name[ar]` is in
67 desktop entries, and all 20 help pages are under
`share/zacos9/help/ar/`. HarfBuzz shapes Arabic, FriBidi orders it, and
Arabic lists and labels right-align. A native speaker's review and text-input
testing are next ([Languages](docs/i18n.md)).

**Russian**: `po/ru.po` is complete at 817 strings, including the Finder's
dialogs; all 20 help pages are under `share/zacos9/help/ru/`. Cyrillic uses
the existing DejaVu Sans fallback and needs no input method; a native
speaker's review is next ([Languages](docs/i18n.md)).

**Spanish**: `po/es.po` is complete at 817 strings, including the Finder,
menu bar, application names, installer and Setup Assistant. The Spanish
glyphs, including `¿` and `¡`, are in the bitmap faces; all 20 help pages
are under `share/zacos9/help/es/`. A native speaker's review is next
([Languages](docs/i18n.md)).

**Brazilian Portuguese**: `po/pt.po`, installed as `pt` for
`pt_BR.UTF-8`, is complete at 817 strings, including the Finder, menu bar,
application names, installer and Setup Assistant. All 20 help pages are
under `share/zacos9/help/pt/`; a native speaker's review is next
([Languages](docs/i18n.md)).

**French**: `po/fr_CA.po` and `po/fr_FR.po` are complete at 817 strings
each, installed as `fr_CA` and `fr_FR`; `Name[fr_CA]` and `Name[fr_FR]` are
in 67 desktop entries, and all 20 help pages are under
`share/zacos9/help/fr_CA/` and `share/zacos9/help/fr_FR/`. English (Canada)
and English (United Kingdom) are selectable English locales. The installer
and Text panel show country flags. Native speakers should review the French
terms and help books ([Languages](docs/i18n.md)).

**Making the project easier to work on**: ten of the eleven phases are done,
each its own pull request, and released as 1.1.0. Phase 7's four starter tasks
are drafted, to be posted as "good first issue"s:

| Phase | What | State |
|---|---|---|
| 0 | Baselines (installed files, warnings) and one C++ standard | Done |
| 1 | This page, CONTRIBUTING.md, ARCHITECTURE.md, templates | Done |
| 2 | Shared code out of the Sniffer's folder into libraries; a `meson.build` per program | Done: shared code in `shell/common/` (libzacos9-shell) and `shell/files/` (libzacos9-files), a `meson.build` per program |
| 3 | One way to run a program and wait for it | Done: `shell/common/process.h` (and `lib/locate` to find programs) |
| 4 | One reader for `key=value` settings files | Done: `shell/common/conf.h` |
| 5 | One file per control panel | Done: each registers itself (`shell/panels/panelregistry.h`) |
| 6 | A small library for the C Wayland programs | Done: shared-memory buffers (`lib/wlclient`) |
| 7 | Small duplicates as "good first issue"s | Drafted: four issues, to be posted |
| 8 | Tests in `tests/<part>/`, with a shared harness | Done: every test under `tests/`; `tests/harness/zacos9test.py` |
| 9 | The root helpers under `system/` | Done |
| 10 | No compiler warnings | Done: CI fails if a change adds one |

## Open issues

All of them are [on GitHub](https://github.com/chromasystemprofessional/Zac-OS-9/issues).

- [#136](https://github.com/chromasystemprofessional/Zac-OS-9/issues/136):
  the remaining language work, including Japanese input.
- [#86](https://github.com/chromasystemprofessional/Zac-OS-9/issues/86):
  video instability with multiple monitors on multiple graphics cards.

## Known gaps

- **SMB**: no share-list dialog yet, and nothing has been tested against a
  real SMB server ([Networking](docs/network.md)).
- **Network Browser**: no retry if `avahi-daemon` isn't running yet when it
  scans. Network volumes can't be dragged; ⌘Y or File ▸ Put Away ejects one.
- **Software**: no search and no installing several at once, on purpose
  ([Software](docs/appstore.md)).
- **Get Info** on a curated folder (System Folder, Applications) opens
  nothing: there's no file behind it to describe.

## Next

1. **Check on real hardware** what was tested only headless or in a VM:
   Mirror Displays switched on and off, a Limited account logging in, a disc
   in a USB drive (AutoPlay), Disk Setup's partition map and the installer's
   "next to what's on it" on a real disk, and a real Windows starting after
   its partition was made smaller.
2. **SMB**: a server to test against, then the share-list dialog.
3. **Classic seamless windows** ([Classic](docs/classic-seamless.md)): the
   capture experiment the feasibility gate is waiting on.
4. **From the [roadmap](docs/ROADMAP.md)**: a Platinum boot menu (on Esc),
   manual pages, trying it in Hyper-V.

## Test servers

For the AFP client tests ([Networking](docs/network.md)):

| Server | Address | Credentials |
|---|---|---|
| Netatalk (this machine) | `127.0.0.1:548` | `platest` / `Sesame-42`, volume `platest's home` |
| Mac OS 9 (Classic) | `127.0.0.1:5548` | `^Mac User` / `macpass`, volume `Macintosh HD` |

## Keeping this page

Change it when the state changes: something finished, started, put on hold or
found missing. A pull request adds no "what I did" row: that goes in its
description and commits.

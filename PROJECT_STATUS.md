# Project status

Updated: 2026-11-12. Latest release: **1.18.2**
([releases](https://github.com/chromasystemprofessional/Zac-OS-9/releases),
[ISO](https://sourceforge.net/projects/zac-os-9/files/)).

This page is what's true now and what's next. What changed and when is in
`git log`, `debian/changelog` and the release notes; how each part works is in
[`docs/`](docs/) and [ARCHITECTURE.md](ARCHITECTURE.md).

## Where things stand

- **The Mac OS 9 plan is built.** Every work package in
  [`docs/mac-os-9-gap-plan.md`](docs/mac-os-9-gap-plan.md) (milestones 1–9,
  83 packages) had landed by 2026-10-20.
- **Removable media is built**: data and audio CDs/DVDs, blank discs, PC and
  USB floppy drives, Iomega Zip/Jaz, generic and Mac disk images,
  single-data-track CUE/BIN images, one eject path, and VLC AutoPlay
  ([Removable media](docs/removable-media.md)). Hardware validation remains
  (see Next).
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
- **1.10.0** makes removable media a first-class part of the desktop
  (CDs, DVDs, audio CDs, blank discs, floppy/Zip disks, USB storage and
  disk images, with AutoPlay in VLC) and makes the boot splash and login
  screen follow the main display's rotation.
- **1.11.0 is Bloodhound 1.0**: Sherlock's search dog is its own program,
  with a Files channel, a one-window search, results that open, reveal and
  drag out, and a worker that stops when asked
  ([Bloodhound](docs/bloodhound.md)).
- **1.12.0 is Bloodhound 1.2**: the search dog gets the whole Find engine
  (criteria rows, contents with snippets, Find Similar Items and saved
  searches), Internet Search Sites, and Web and user channels; it is called
  **Bloodhound (Search)** so its purpose is clear
  ([Bloodhound](docs/bloodhound.md)).
- **1.13.0 finishes Bloodhound 1.3**: live search, recent searches, the
  Index menu's status and controls, folder and search-site drops, a
  collapsible preview pane, and the app's strings in the translation files
  ([Bloodhound](docs/bloodhound.md)).
- **1.14.0 runs the standard application shortcuts**: ⌘Q quits the program
  in front, closing all its windows so it can ask about unsaved work, ⌘H
  hides it, and ⌘M collapses or expands its front window; the Sniffer keeps
  ⌘H and ⌘M for its File menu ([Roadmap](docs/ROADMAP.md)).
- **1.14.1 puts disks mounted from /etc/fstab on the desktop**, such as an
  internal data drive added after installation, which GIO never reports as
  a volume ([Files](docs/files.md)).
- **1.14.2 states the privacy promise**: no telemetry, analytics, online
  account, advertising ID, device fingerprint or first-party server; every
  network connection is listed in [PRIVACY.md](PRIVACY.md) and a test
  enforces it ([Privacy](PRIVACY.md)).
- **1.14.3 keeps desktop disks visible**: a disk whose remembered place
  overlapped another icon's is moved to a free place instead of being
  drawn under it ([Files](docs/files.md)).
- **After Dark modules can be imported** into the Screen Saver panel from a
  disk image the user supplies, and run in the saver through the vendored
  MIT 68k engine; no modules or fonts ship ([After Dark
  modules](docs/after-dark.md)).

## Being worked on

**Bloodhound (Search)**: the planned 1.0–1.3 slices are done; 1.3 shipped in
1.13.0. The later ideas — optional local channels, Smart Folders, a global
hotkey and local semantic search — are not scheduled
([Bloodhound](docs/bloodhound.md)).

**The System Folder**, made to work the way Mac OS 7 to 9's did, item by
item: [`docs/system-folder.md`](docs/system-folder.md) has every item, what
ZacOS has for it, and the order. All six items are done.

**Monitors**: each screen's resolution, pixel size and rotation are
applied live (`compositor/src/prefs.c`): rotation is
`display.NAME.rotation` (0, 90, 180 or 270 degrees), the layout uses the
rotated logical size, and `display-rotation` tests it. The main screen's
rotation is also recorded machine-wide, so the login screen and the boot
splash -- the kernel console and Plymouth -- follow it through
`boot/zacos9-rotate-boot` ([Starting up](docs/boot.md#rotated-screens)).

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

**Japanese (display and input pilot)**: `lib/text.c` falls back through
Fontconfig to Noto Sans CJK, `wrapText()` breaks Japanese without spaces,
and `po/ja.po` translates the Mouse panel, every control panel's name and
title, and the Mouse desktop entry. Input works through Fcitx 5 and Mozc:
the compositor relays input-method-v2 to text-input-v3, and a login item
starts Fcitx only in a Japanese session. The catalog is still small; a
native speaker's review and the other panels are next
([Languages](docs/i18n.md)).

**Arabic**: `po/ar.po` is complete at 818 strings, including the Finder,
menu bar, application names, installer and Setup Assistant; `Name[ar]` is in
67 desktop entries, and all 20 help pages are under
`share/zacos9/help/ar/`. HarfBuzz shapes Arabic, FriBidi orders it, and
Arabic lists and labels right-align. A native speaker's review and text-input
testing are next ([Languages](docs/i18n.md)).

**Russian**: `po/ru.po` is complete at 818 strings, including the Finder's
dialogs; all 20 help pages are under `share/zacos9/help/ru/`. Cyrillic uses
the existing DejaVu Sans fallback and needs no input method; a native
speaker's review is next ([Languages](docs/i18n.md)).

**Spanish**: `po/es.po` is complete at 818 strings, including the Finder,
menu bar, application names, installer and Setup Assistant. The Spanish
glyphs, including `¿` and `¡`, are in the bitmap faces; all 20 help pages
are under `share/zacos9/help/es/`. A native speaker's review is next
([Languages](docs/i18n.md)).

**Brazilian Portuguese**: `po/pt.po`, installed as `pt` for
`pt_BR.UTF-8`, is complete at 818 strings, including the Finder, menu bar,
application names, installer and Setup Assistant. All 20 help pages are
under `share/zacos9/help/pt/`; a native speaker's review is next
([Languages](docs/i18n.md)).

**French**: `po/fr_CA.po` and `po/fr_FR.po` are complete at 818 strings
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
  the remaining language work.
- [#86](https://github.com/chromasystemprofessional/Zac-OS-9/issues/86):
  video instability with multiple monitors on multiple graphics cards.

## Known gaps

- **Keyboard menus**: menus are mouse-only (no way in from the keyboard, no
  arrow or Return walking), and the menu bar itself doesn't run its items'
  ⌘ shortcuts; the apps' own shortcuts and ⌘Q, ⌘H and ⌘M work
  ([Roadmap](docs/ROADMAP.md)).
- **SMB**: no share-list dialog yet, and nothing has been tested against a
  real SMB server ([Networking](docs/network.md)).
- **Network Browser**: no retry if `avahi-daemon` isn't running yet when it
  scans. Network volumes can't be dragged; ⌘Y or File ▸ Put Away ejects one.
- **Software**: no search and no installing several at once, on purpose
  ([Software](docs/appstore.md)).
- **Get Info** on a curated folder (System Folder, Applications) opens
  nothing: there's no file behind it to describe.
- **Removable media hardware checks**: PC/USB floppy and Zip media-change
  polling, loop-image teardown, and real-disc AutoPlay have not been verified
  on hardware. Audio and multi-track CUE/BIN images and Blu-ray are not
  supported ([Removable media](docs/removable-media.md)).

## Next

1. **Check on real hardware** what was tested only headless or in a VM:
   Mirror Displays switched on and off, a Limited account logging in, a disc
   in a USB drive (AutoPlay), a floppy and a Zip disk (insertion and Put
   Away), a UDF DVD and a FAT floppy image, Disk Setup's partition map and
   the installer's "next to what's on it" on a real disk, and a real Windows
   starting after its partition was made smaller.
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

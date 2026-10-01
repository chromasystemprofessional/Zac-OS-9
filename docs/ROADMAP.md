# Roadmap

Target: Mac OS 8.5–9.x Platinum, **strict replica** (1× pixel art, integer
scaling only, original behaviors and timings).

## Phase 0: Environment ✅
- WSL Debian 13 toolchain: wlroots 0.18, Qt 6.8, meson.
- Repo layout, build and run scripts.
- `platinum-wm` skeleton: desktop fill, xdg-shell windows, keyboard and pointer
  input, nested run under WSLg.

## Phase 1: Compositor / window chrome ✅
- ✅ Server-side decorations (xdg-decoration, KDE server-decoration): Platinum
  title bar with stripes, close / zoom / collapse boxes, resize box, active and
  inactive states. Pixel-tested against the HIG figures.
- ✅ Outline drag and outline resize (Mac OS 9 had no live dragging by default).
- ✅ WindowShade collapse: collapse box, Option-click for all windows,
  double-click on the title bar.
- ✅ Zoom (Mac OS has no maximize).
- ✅ Xwayland: X11 windows framed; menus and tooltips unframed.
- ✅ Integer output scaling (`-S 2` / `PLATINUM_SCALE=2`) with nearest-neighbour
  filtering for the frame.
- Deferred:
  - Window layering by application (needs app identity from Phase 2).
  - Measured box pressed states and drag-outline pattern.
  - Real Charcoal-metric font (Phase 6).

## Phase 2: Menu bar (in progress)
- ✅ Menu bar and menu painter measured from HIG figures 4-1, 4-2 and 4-3
  (`docs/reference/platinum-menus.md`), pixel-tested.
- ✅ `platinum-menubar`: a C layer-shell client using the shared painter
  rather than Qt, so the drawing is pixel-exact.
  - Platinum logo menu (original mark) fed from `Platinum Menu Items`.
  - Front app's File and Edit menus; Finder menus when no app is in front.
  - Clock and Application menu (hide, show, switch; switching brings all of
    an app's windows forward).
  - Press-drag-release and sticky-click tracking, item blink.
- ✅ Compositor: layer-shell, foreign-toplevel, hidden windows, ⌘ = Super → Ctrl,
  virtual pointer and keyboard (used for scripted UI tests with `wlrctl`).
- Next:
  - ✅ Edit menu commands (keystrokes to the front app via a virtual keyboard).
  - Global app menus via `com.canonical.dbusmenu` / KDE appmenu.
  - ⌘-key equivalents for menu items.
  - App icons in the Application menu.
  - ✅ Clicking the clock shows the date.
- To measure: selected-item look, blink count and timing, the other screen
  corners, other accent-color triples.

## Phase 3: Finder ✅ (core)
- ✅ Scroll bars, push buttons, dialog frame, list view: measured from the
  HIG (`docs/reference/platinum-finder.md`) and pixel-tested.
- ✅ `platinum-finder` (Qt6 + LayerShellQt, all drawing through `lib/`):
  - Desktop (layer surface): original pattern, disk, Trash, `~/Desktop` items.
  - Spatial windows that remember position, size, view and sort
    (`platinum-shell-v1` lets the compositor place them).
  - Icon view and list view (sortable columns, disclosure triangles).
  - Select, open, drag and drop (move, copy, Option-copy, to the Trash),
    rename in place, New Folder, Move To Trash, Empty Trash (with alert),
    Get Info, About This Computer.
  - Finder menus driven over a socket from the menu bar; ⌘-keys.
  - Free icon positions in icon view (drag to place; remembered).
  - Button view; Duplicate, Make Alias (symlinks), Put Away, Show Original.
  - Spring-loaded folders; Get Info comments (`user.xdg.comment`).
  - Find (File > Find…): names, whole disk, results as a Finder view.
  - Labels from File > Label (the menu bar's first hierarchical menu),
    stored in `user.platinum.label`; labelled icons are tinted.
  - Alias names in italics.
- To measure: icon grid and label metrics, Get Info layout, alert margins,
  selected list row, expanded triangle.

## Phase 4: Classic app emulation (in progress)
- Done:
  - SheepShaver (PowerPC, up to Mac OS 9.0.4) and Basilisk II (68k, up to
    8.1), built from source by `emulation/build-emulators.sh`.
  - `platinum-classic`: picks ROMs by checksum and disk images by signature
    from `~/Classic`, shares the home folder as the Mac's "Unix" volume,
    and makes a blank disk for installing from a CD image.
  - Logo menu > Classic; double-clicking a Mac disk image boots it; alerts
    explain a missing ROM or emulator.
  - Mac OS 9.0 installed from the user's CD image onto a blank disk and
    booted from it; the home folder shows up as the "Unix" volume.
- Next:
  - Double-clicking a classic app or document launches it in the emulator.
  - SDL's Wayland backend under platinum-wm (Xwayland for now).
  - Packaging the emulators (Phase 7).

## Phase 5: Control Panels and theming (in progress)
- Done:
  - Group box, list box and tab control, each matching its HIG figure
    (2-38, 2-25, 2-30) pixel for pixel apart from label text.
  - Accent colours as a live setting (Lavender and Ivy measured, six
    derived), and a highlight colour.
  - Appearance control panel (Color, Desktop, Sound tabs), opened from
    the logo menu's Control Panels submenu; changes apply at once.
  - Desktop entries in share/applications; platinum-wm puts its ../share
    on XDG_DATA_DIRS so the menu bar names our apps.
  - Checkbox, little arrows, clock control and edit-text frame, matching
    HIG figures 2-8, 6-1, 2-20 and 2-22 pixel for pixel.
  - Date & Time control panel: date and time in clock controls, time
    zone picker and network time (timedated, via timedatectl), and the
    menu bar clock's options (hidden, 24-hour, day of the week), which
    the menu bar follows live.
- Next:
  - Mouse, Keyboard, Monitors, Sound volume: wired to Linux backends
    (PipeWire, wlr-output-management...).
  - Pop-up menu buttons (for panels that need them).
  - Platinum QStyle plugin and GTK3/4 theme, so third-party apps match.

## Phase 6: Original assets (in progress)
- Done:
  - Fonts: Platinum System 12 and Platinum Views 9, original bitmap fonts
    with the measured Charcoal 12 / Geneva 9 metrics (every measured HIG
    string lays out to the pixel), composed accented letters, synthesised
    italic for alias names, DejaVu fallback for anything else
    (docs/reference/platinum-fonts.md).
  - Icon set: folder, document, application, hard disk, Trash (empty and
    full) and caution, as 32x32 and 16x16 pixel art in
    assets/icons/platinum-icons.picon (tools/icons/build_icons.py).
  - Desktop patterns: ten original tiles (embossed 64x64 textures in a
    small palette, and 8x8 classics) from tools/patterns/make_patterns.py;
    chosen by `pattern=` in ~/.config/platinum/desktop.conf, applied live.
  - Cursors: a "Platinum" Xcursor theme (arrow, I-beam, animated watch,
    crosshair, hand, move/resize, not-allowed, copy/alias/help) from
    assets/cursors, used by platinum-wm and every client it starts.
  - Alert sounds: six original synthesised sounds (assets/sounds, from
    tools/sounds/make_sounds.py); alerts play the one named by
    `alert-sound=` in desktop.conf (default "platinum").
  - Startup screen: a Welcome box with the logo and the HIG progress bar
    over the desktop pattern until the menu bar and desktop are up.
  - New logo (a platinum sparkle, clearly not Apple's), and icons for
    applications (a Platinum window), Mac disk images and classic
    applications (recognised from SheepShaver's .finf type 'APPL').
- Next:
  - Choosing the pattern and alert sound from the Appearance and Sound
    control panels (Phase 5).
  - An application font (Geneva 12 metrics) for app content.

## Phase 7: Packaging and distribution
- `.deb` packages and a `platinum-2026.desktop` Wayland session file.
- Debian VM image for full-session testing (Hyper-V or QEMU).
- Live and installable ISO built with `live-build`.

## Fidelity references
`docs/reference/` will hold measurements (pixel coordinates, colors, timings)
taken from period documentation and real Mac OS 9 running under emulation. No
Apple bitmaps are copied into `assets/`.

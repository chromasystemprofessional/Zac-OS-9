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
- Next:
  - Alias names in italics (needs an italic views font).
- To measure: icon grid and label metrics, Get Info layout, alert margins,
  selected list row, expanded triangle.

## Phase 4: Classic app emulation
- Package SheepShaver (PowerPC, up to Mac OS 9.0.4) and Basilisk II (68k, up to 8.1).
- Shared folder bridge between the Finder and the emulated Mac.
- Double-clicking a classic app or document launches it through the emulator.
- Setup assistant that checks the user-supplied ROM and system disk.

## Phase 5: Control Panels and theming
- Appearance, Date & Time, Sound, Monitors, Mouse, Keyboard: wired to
  Linux backends (systemd-timedated, PipeWire, wlr-output-management…).
- Platinum QStyle plugin and GTK3/4 theme, so third-party apps match.

## Phase 6: Original assets
- Bitmap system and application fonts (Chicago and Geneva style), drawn from scratch.
- Icon set, desktop patterns, cursor theme, alert sounds, startup screen.

## Phase 7: Packaging and distribution
- `.deb` packages and a `platinum-2026.desktop` Wayland session file.
- Debian VM image for full-session testing (Hyper-V or QEMU).
- Live and installable ISO built with `live-build`.

## Fidelity references
`docs/reference/` will hold measurements (pixel coordinates, colors, timings)
taken from period documentation and real Mac OS 9 running under emulation. No
Apple bitmaps are copied into `assets/`.

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

## Phase 2: Menu bar
- Layer-shell menu bar client (Qt6): Apple menu, application menus, clock,
  Application menu (top right).
- Global menus via `com.canonical.dbusmenu` / KDE appmenu protocol.
- Menu tracking, highlight, and blink timing measured against reference captures.

## Phase 3: Finder
- Desktop icons, spatial folder windows (each folder remembers its window
  position and view), icon, button and list views.
- Trash, Get Info, labels, spring-loaded folders, aliases (symlinks).
- File type and creator mapping to MIME types.

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

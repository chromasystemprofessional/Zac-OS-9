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
  - Radio buttons and the slider (HIG figures 2-4, 2-17), pixel for pixel.
  - Mouse (tracking speed, double-click speed), Keyboard (key repeat rate
    and delay), Sound (volume and mute through pactl, alert volume) and
    Monitors (resolution, 1x/2x/3x pixel size) panels in
    platinum-controlpanel. platinum-wm applies the mouse, keyboard and
    screen settings live (compositor/src/prefs.c) and publishes the
    screen's modes for the Monitors panel.
  - Pop-up menu buttons and their menus (HIG figures 2-6, 2-7, 3-25),
    pixel for pixel; the menu highlight measured from figure 2-7. Edit
    text fields (one or more lines, selection, ⌘A/C/X/V) and the
    Mac OS 9 "Save changes?" alert with Don't Save.
  - TCP/IP control panel (platinum-tcpip), after Mac OS 9's, on
    NetworkManager: Connect via, Configure (DHCP or manually), IP
    address, subnet mask, router, name servers, search domains; saved on
    closing, after asking. Modern additions: a Wi-Fi Network pop-up
    (signal bars, padlocks, Other Network…, Wi-Fi on/off), joining with
    a password (never on a command line), Options… (IPv6, private Wi-Fi
    address, connect automatically) and Info…. Tested against a stand-in
    nmcli (tests/ui/tcpip.sh) and real NetworkManager with a simulated
    WPA2 network in the VM (tests/vm/tcpip-wifi.py).
  - File Sharing control panel (platinum-filesharing), after Mac OS 9's:
    - Start/Stop: Network Identity, File Sharing for Macs (AFP through
      Netatalk 4: Mac OS 8.5 to 9 over TCP/IP with DHX, older Macs with
      clear-text passwords if allowed, macOS with DHX2 and Bonjour), and
      Windows File Sharing (SMB through Samba, wsdd2 for Windows'
      Network) where Mac OS 9 had Program Linking.
    - Activity Monitor (connected users, Disconnect) and Users & Groups
      (owner, guests).
    - The owner's password is their login password; it is stored for
      Samba once checked.
    - Changes go through platinum-sharing-helper (pkexec; polkit lets
      administrators at the computer use it without a password).
    - Tested: Mac OS 9 in Classic connected with the Chooser, mounted the
      owner's home and wrote to it; smbclient against Samba in the VM.
  - Classic's emulated Mac has a network (SheepShaver's NAT): it reaches
    this computer at 10.0.2.2.
- Next:
  - File sharing: sharing folders from Get Info (Sharing, with
    Owner/User/Group/Everyone privileges); the Network Browser to connect
    to other computers: Windows and macOS through gvfs, and classic Macs
    (AFP 2.2, which nothing in Debian speaks) through our own client.
  - TCP/IP: 802.1X (enterprise) networks, VPNs, proxies; a Wi-Fi menu
    in the menu bar.
  - Monitors on real hardware: several screens, arrangement.
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

## Phase 7: Packaging and distribution (in progress)
See `docs/packaging.md`.
- Done:
  - `platinum-2026` and `platinum-emulators` (SheepShaver and Basilisk II
    from a pinned macemu commit, with their source) `.deb` packages, built
    from what git would commit (`scripts/build-debs.sh`).
  - The login session: `platinum-session` and a `platinum-2026.desktop`
    entry in `wayland-sessions`. Software rendering when there's no GPU, a
    session log, and the environment shared with D-Bus and systemd.
  - Ctrl+Alt+F1 to F12 switch consoles on a real screen.
  - Live and installable ISO with live-build (`scripts/build-iso.sh`):
    - greetd and tuigreet for login;
    - the live user logs straight in;
    - Debian's installer is on the boot menu;
    - firmware for real hardware.
  - QEMU virtual machine for full-session testing (`vm.cmd`,
    `scripts/vm.sh`), and a headless boot test (`tests/vm/boot.sh`).
  - A silent start-up: white from GRUB (menu hidden; Esc shows it)
    through the kernel and a white plymouth theme (the console palette is
    white while booting), then platinum-wm's logo on white, the Welcome
    screen and the desktop. No console text.
  - `scripts/dev-boot.sh`: try system changes in a VM in minutes, on top of
    the last ISO build's system, with a recorded timeline of the screen.
- Next:
  - A Platinum login window in place of tuigreet.
  - A Platinum boot menu (shown on Esc).
  - Manual pages.
  - Testing on real hardware and in Hyper-V.

## Fidelity references
`docs/reference/` will hold measurements (pixel coordinates, colors, timings)
taken from period documentation and real Mac OS 9 running under emulation. No
Apple bitmaps are copied into `assets/`.

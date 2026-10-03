# Project Status

Updated: 2026-10-03

## Completed

| Commit | Feature |
|---|---|
| `cd4f6ed` | Silent start-up: white screen, platinum sparkle logo, Welcome screen; `scripts/dev-boot.sh` |
| `d1f5765` | File Sharing control panel (AFP + SMB toggle, shared folders list) |
| `02cd5a4` | Get Info > Sharing view, pixel-matched to Mac OS 9's original |
| `2349968` | `network/afp/zacos9-afp`: AFP 2.x FUSE client skeleton |
| `3dbae73` | AFP client fully working: single-`sendmsg` fix, busy-delete retry, truncate fallback; `tests/sharing/afp-client.sh` passes on Mac OS 9 and Netatalk |
| *(uncommitted)* | Virtual Macintosh filesystem for the Finder, with real application icons, package origin, and hide-not-uninstall — see below |
| *(uncommitted)* | Software window (app catalog over apt) in the Apple menu — see below |
| *(uncommitted)* | Network Browser: connect to AFP and SMB servers, mounted volumes on the desktop — see below |
| `4e3ebe6` | ZacOS 9 rename (from Platinum 2026) |
| `996b311` | Applications: empty on fresh install, flat launchers |
| `634add8` | Finder: local disk volumes on the desktop (GVolumeMonitor) |
| `8771336` | Windows app compatibility: `.exe`/`.msi` open via Wine |
| *(uncommitted)* | Mac OS 9-style installer: Welcome → Select Disk → Installing → Done, runs `zacos9-install` via polkit; partition-level install (preserve FAT/HFS+ sibling partitions) |
| *(uncommitted)* | New ZacOS 9 SVG logo at 16×16 (menu bar) and 64×64 HQ (startup, installer) with real alpha blending |
| *(uncommitted)* | Installer fixed end to end: GRUB packages in the image, UEFI/BIOS detection, BIOS boot partition, fallback `EFI/BOOT/BOOTX64.EFI` for Macs, live user in `sudo` (polkit), failing step shown in the window. VM-tested (`build/test-install.py`): whole disk under BIOS and UEFI, and a partition beside a kept FAT partition under UEFI — each installed disk boots to the desktop. Boot splash now shows the logo and a progress bar. |

## Current work

**Software window: look-and-feel presets (GIMP › Photoshop Layout)** (built and tested; not yet committed; not in an ISO yet).

- A catalog item can carry a `"style"`; the detail pane then shows a second button (Photoshop Layout /
  Standard Layout) once the app is installed, with a line saying what it does and "Photoshop Layout is
  on." in the status. `appstore/zacos9-appstyle` (Python, runs as the user) does the work — see
  `docs/appstore.md`. `debian/control` now Depends on `python3` and `procps`.
- Checked first: Debian 13 has GIMP 3.0.4 (PhotoGIMP 3.1 needs 3.0+); PhotoGIMP is GPL-3.0; the
  splash/icons/launcher artwork has no stated license, so they're not used.
- Tested for real: in the Software window with GIMP installed — apply, GIMP launched with the layout
  (single slim tool column, tool options and layers on the right), then Standard Layout restored an
  existing settings file exactly. Found and fixed: the preset's `sessionrc` holds its author's
  2560-wide, two-monitor window sizes and positions (GIMP opened off-screen); those are cut out.
- 19 checks in `tests/store/test_appstyle.py` (meson test `appstyle`). Krita/Inkscape presets not done:
  their built-in shortcut schemes still to be confirmed in the Debian versions (Krita 5.2.9, Inkscape 1.4).

**Boot screens: logo from the first moment, a new Welcome screen** (built; checked in QEMU under BIOS and UEFI, live and installed; not yet committed; not in an ISO yet).

- **GRUB shows the logo** (centred, on white) from the moment it starts, where there was a blank
  white or black screen — live medium (`iso/config/bootloaders/grub-pc`: 800x600 picture
  `zacos9-boot.png`; `isolinux/splash.png` + hidden countdown for BIOS) and installed
  (`boot/09_zacos9`; `zacos9-install` runs `boot/zacos9-bootlogo` — stdlib-only Python writing a
  white PNG the size of the current screen (fb0) with the logo in the middle — and sets
  `GRUB_GFXMODE=WxH,auto`). GRUB can only draw backgrounds at the top left, hence a picture the
  screen's size. GRUB's own text is hidden by `color_normal=white/black`. Past 4K it's skipped.
  Debian's `00_header` hardcodes `background_image -m stretch`, which is fine for an exact-size picture.
- **Welcome screen** (`compositor/src/startup.c`): "Welcome to ZacOS 9", a vector picture of a modern
  computer (`lib/welcome.c`, cairo; display on a stand showing a ZacOS desktop, keyboard, mouse) in a
  white well, status, progress bar; the extension icons now march in along the bottom of the screen
  as the bar fills (they used to run during the logo phase). Logo-only phase 1500 → 500 ms.
  `lib/tests/test_welcome.c`; `tests/vm/boot-frames.py` recognises the new box.
- Tools: `tools/boot/make-boot-art.py` regenerates `boot/logo-64.rgb` and the two live pictures.
  `build/bootlab/` (not committed): loop-mount the test disks, patch an ISO's boot files with xorriso
  (no rebuild), record a boot frame by frame (`rec.sh`, `rec-iso.sh`, `analyze.py`).
- **Not fixed — gaps of ~1 s of plain white** between GRUB and the splash, and before zacos9-wm
  draws (the kernel's text console clears the screen to its all-white palette when it starts; and
  plymouth quits before the wm's first frame). Tried `fbcon=map:9` (keeps the logo up through the
  first gap) but it leaves a *black* gap after plymouth and unbinds the text consoles (the greeter
  needs them) — would need a seamless hand-off (plymouth deactivate / quit --retain-splash around
  wm start, fbcon bound after) that can't be validated on real hardware from here.

**Disk name, aliases of applications, About logo** (built and tested; not yet committed; not in an ISO yet).

- The startup disk: "Macintosh HD" (Apple's) replaced. Name = the user's rename, else the name
  given at install (`/etc/zacos9/disk-name`), else "Zacintosh HD"; a registry still holding the
  old default is treated as unnamed. The installer's Select screen has a "Disk name" field
  (default Zacintosh HD, ≤ 27 chars, no `:` `/`), passed as `zacos9-install DEVICE NAME`, also
  used as the ext4 label (16 bytes). The desktop's disk can now be renamed (it was "fixed").
  VM-tested: installed as "Adam's Disk", label and file right, shown on the desktop after setup.
- Make Alias on a Macintosh-view item (an application, or a folder standing for a real one) makes
  the alias on the desktop (it silently failed before). An application's alias is a link to its
  desktop file: shown with the app's icon, opening it launches the app (`appLaunchFile`).
- About This Computer: the 64x64 logo (was the 16x16 scaled 3x).
- Installer disk list leaves out RAM disks (`lsblk -e 1,7,11`).

**Installing apps by dropping them onto Applications** (built and tested in a nested session; not yet committed; not in an ISO yet).

- Files dropped on the Applications icon or into its window (`vfsIsApplications`; Applications
  now accepts drops) are installed, not moved: `.exe`/`.msi` to the Windows Installer, the rest
  to the new App Installer (`shell/appinstall/`), a Mac OS 9 copy-style window ("Items remaining
  to be installed", progress bar, Stop).
  - `.deb`: an alert names package, version and publisher (setup runs as root), then
    `zacos9-appstore-helper install-deb FILE` (new: validates, copies the file, `apt-get install`
    with `APT::Status-Fd` for real progress; dependencies fetched). Not stoppable once started.
  - `.AppImage`: copied (chunked, with progress) into `~/.local/share/zacos9/apps`, made runnable;
    name/icon from its own root files via `--appimage-extract`, following their symlinks into
    `usr/share` (extracting a link alone left it dangling). `libfuse2t64` added to the ISO.
  - `.tar.gz/.xz/.bz2/.zst`: unpacked there with progress; a desktop entry inside, else exactly
    one program (or one named like the archive), else refused and removed.
  - Anything else: "isn't an application to install" (shown by the App Installer).
- The Finder now loads `Icon=` file paths (GFileIcon), so these apps show their own icons.
- **Compositor focus bug fixed** (`view_focus`, view.c): after a click or drag on the desktop
  (a layer surface with on-demand keyboard), newly opened windows were drawn active but typing
  went to the desktop until clicked. Only an *exclusive* layer (a menu being tracked) keeps it now.
- Tested: .deb (with a real dependency fetched), a real AppImage (appimagetool), an archive with
  one program, one with two (refused), notes.txt (refused); drops on the icon and into the window;
  the installed archive app launched from Applications.
- Not done: removing such apps (Move To Trash still only hides; `X-ZacOS9-Installed` in their
  entries records what to delete when that's added). Flatpak `.flatpakref` not handled.

**Finder: moving icons in the Macintosh view, Clean Up, Arrange** (built and checked on screen; not yet committed).

- Icons of virtual items (Macintosh HD's three folders, System Folder, Applications) used to refuse
  to drag at all. They now drag within their own window and stay put (remembered per folder, as
  for real folders), carrying `ICON_MOVE_MIME` rather than file URLs, so nothing else accepts them.
  Dropped anywhere else, an application gets an alert: it stays in Applications; Move To Trash
  removes one. Other virtual items: "part of this computer and can't be moved".
- View > Clean Up (nearest free grid spot, topmost-leftmost first) and View > Arrange > by Name /
  Date Modified / Date Created / Size / Kind / Label (`items.cpp` `arrangeIcons`/`sortIcons`), for
  an icon-view window, or the desktop with no window in front (the Trash keeps its corner).
- Checked in a nested session: Documents moved and still there in a new session; Clean Up snapped
  it to the grid; Arrange by Name ordered the disk's folders; dragging an app out of Applications
  showed the alert; desktop Clean Up / Arrange.

**Windows Installer** (built and tested in a nested session, not yet committed; not in an ISO yet).

- `shell/wininstall/`: drop a .exe/.msi on its window (from the Finder or a browser), or
  double-click one in the Finder (`Finder::launchWindows` now opens it here — and no longer uses
  `waitForFinished`). Install (first time: `wineboot --init` with a progress sweep), or Open
  Without Installing (.exe only). Afterwards it names the programs added — the new desktop
  entries Wine files under `~/.local/share/applications/wine/`, which Applications shows.
  Apple menu > Windows Installer. Closing is refused while Wine is running the installer.
- Wine in the ISO: `iso/config/hooks/normal/8000-zacos9-wine.hook.chroot` adds i386 and installs
  `wine wine64 wine32:i386 fonts-wine` (Debian 13's wine64 has no WoW64 — `i386-windows` is
  empty — so 32-bit installers need wine32). **Untested until the next ISO build.**
- Tested with a real 32-bit NSIS installer (`build/wintest/`, made with `makensis`): dragged from
  the Finder desktop onto the window, Install, Wine's first-run setup, the NSIS pages clicked
  through; window then said "Installed: ZacTest Notes." (its uninstaller left out); the program
  appeared in Applications with its icon and opened (Notepad with the installed file).
- Adobe Creative Cloud: deliberately skipped (its installer and app don't run under Wine).
- `shell/wine/zacos9-wine` is no longer used by the Finder (still installed).

**Applications shows only what's been installed; list scrolling** (built and tested, not yet committed; not in an ISO yet).

- Applications used to require `OnlyShowIn=ZacOS9`, which hid real programs (Firefox from
  Software) and showed ZacOS 9's own control panels. Now: every launchable entry *except*
  ZacOS 9's own (`zacos9-*`), the ones the image was built with (the ISO hook writes
  `/usr/share/zacos9/base-applications`), and Wine's "Uninstall …" shortcuts. Windows programs
  installed through Wine appear because Wine files their Start-menu shortcuts as desktop entries
  in `~/.local/share/applications/wine/`. Without the list (an install from an older ISO, a dev
  tree) everything not `zacos9-*` shows. `test-finder-vfs`: 5 new checks, 109 pass.
- List boxes (`PanelList`): the scroll-bar thumb can be dragged, and the scroll wheel scrolls
  (3 rows a notch). Wired into Setup, Installer, Date & Time, Software, Appearance, Network
  Browser and the volume picker. Thumb drag checked on screen in Set Time Zone; the wheel isn't
  (vptr has no axis events).

**Setup Assistant** (first-run account setup, built and VM-tested, not yet committed; not in an ISO yet).
After Mac OS 9's Mac OS Setup Assistant: one window, ◀ ▶ arrows, "Go Ahead" on the last page.
Pages: Introduction, Name (+ short name), Password (+ "Log in automatically", on by default),
Computer Name, Time Zone, Conclusion.

- `shell/setup/` (the window), `setup/zacos9-setup-helper` (root through pkexec; polkit allows
  only the `zacos9-setup` account, and the helper refuses once `/var/lib/zacos9/setup-pending`
  is gone). Input on stdin, so the password is never in a process list.
- `zacos9-install` now removes the live user, `/etc/sudoers.d/live` and live-config's
  `sudo_on_live.rules` from the installed copy, makes the locked `zacos9-setup` account, logs it in
  once via greetd's `initial_session`, and leaves the flag; `zacos9-wm` opens the assistant when the
  flag is there.
- Go Ahead: account (sudo + the live user's groups), password, hostname + pretty name, time zone,
  then greetd restarts into the new account (`/run/greetd.run` removed first — greetd otherwise runs
  `initial_session` only once per boot). Without automatic login, that one-time login is removed
  again so the next start-up asks for the password.
- `build/test-firstrun.py`: install in a VM, drive the assistant by keys, check the disk. Passed:
  account/groups/hostname/greetd all as chosen, setup account and live user gone, and the session
  hands over to the new account's desktop.
- **Text login fixed:** it drew white on white — the boot's all-white console palette was only
  reset by `zacos9-console-colors.service` 20 s after greetd started. greetd now runs
  `session/zacos9-greeter`, which runs `setvtrgb vga` before `tuigreet` (`build/test-greeter.py`:
  readable at once at boot with automatic login off; logging in by keys reaches the desktop).
  A Platinum login window in place of tuigreet is still on the roadmap.
- Seen once, not reproduced: `plymouth-quit.service` taking ~19 s (once timing out at 20 s),
  which holds greetd back that long. Worth watching on real hardware.

**Network Browser** (built, tested end to end against a real AFP server, not yet committed).

Connecting to other computers from the Apple menu, right after Software:
discover servers (Bonjour, and our own WS-Discovery probe for Windows),
Connect to Server, log in, pick an AFP volume, mount it — it then shows
up on the desktop as a disk icon, and ⌘Y/Put Away ejects it. See
[docs/network.md](docs/network.md), which also covers five real bugs
found only by actually connecting end to end (two in `avahi-browse`
invocation, a `QProcess::waitForFinished()` deadlock, a rejected `--`
argument, and `gio mount`'s meaningless exit code) and exactly what
could and couldn't be verified live in this dev container.

- `shell/network/` — `netbrowser.{h,cpp}` (the window), `discovery.{h,cpp}`
  (Bonjour via `avahi-browse`, Windows via a hand-written WS-Discovery
  probe — Debian ships no query tool, only `wsdd2`'s responder),
  `afpclient.{h,cpp}` (wraps the already-built `zacos9-afp
  info`/`volumes`/`mount`), `smbclient.{h,cpp}` (wraps `gio mount`),
  `netvolumes.{h,cpp}` (what's mounted, read from `findmnt`/gvfs's own
  runtime directory — no registry to drift from reality), three dialogs
  (`connectdialog`, `logindialog`, `volumedialog`).
- AFP mounts fully detached (double-fork + a pipe for the password,
  `afpMount()`) at `~/.local/share/zacos9/mounts/<volume name>`, so
  they outlive the GUI the way a real Mac's mounted volumes did.
- `shell/finder/desktop.cpp` polls what's mounted every 3 s and shows
  each as a disk icon (`Item::isNetworkVolume`); `Finder::putAway()`
  ejects instead of trying to move a file. Dragging one is not
  supported yet — inherited from a pre-existing gap (every "fixed"
  desktop item, including the startup disk itself, already couldn't be
  dragged, with its own TODO predating this work).
- Netatalk needs nothing to be Bonjour-discoverable once `avahi-daemon`
  is running (confirmed live: it self-announces via its own
  Bonjour-compatibility library, under the configured server name).
  Samba's own avahi support couldn't be confirmed the same way (`smbd`
  crashes on start in this dev container — unrelated pre-existing
  issue), so `sharing/zacos9-sharing-helper` still writes an explicit
  `_smb._tcp` avahi service file when SMB sharing is on.
- `avahi-utils` and `gvfs-backends` added to `debian/control` Depends.
- Deliberately no SMB share-list dialog: anonymously browsing a
  server's share list needs its root mounted first, and that behaved
  inconsistently testing against this project's own (broken) SMB
  server; the login dialog asks for the share name directly instead,
  the way Windows' own "Map Network Drive" does.

Verified end to end against this project's own Netatalk test server,
through the real menu and real keyboard/mouse input, not just
`meson test`: opened from the Apple menu; it found "Platinum Test (AFP)"
by itself (no manual entry needed) once File Sharing's AFP was turned
on; logged in as `platest`; the volume list showed "Public" and
"platest's home"; mounted "platest's home" (confirmed with `df` and a
real write); watched it appear as a disk icon on the desktop within the
next poll; ⌘Y'd it away (confirmed gone from both the desktop and
`findmnt`). `tests/ui/network-connect.sh` scripts this exact sequence
and was itself run to completion to confirm its coordinates are right.

**Not verified live**: a real SMB mount (`smbd` crashes on start in
this dev container, and no other SMB server was reachable on the dev
network), and WS-Discovery finding an *independent* Windows computer
(only this container's own `wsdd2` responder answered — correctly, but
it proves the protocol code works, not that it finds someone else's
computer on a real network). Both follow their documented protocols/
conventions faithfully; both are the first things to check if they
don't work on a real network.

---

**Software window** (built, tested end to end against the real system, not yet committed).

A curated app catalog in the Apple menu, right after Classic:
category list, item list, details pane with Install/Remove — Mac OS 9
never had one of these, so it's styled after the Mac's own
list-and-details convention (and the Installer VISE packages its CDs
shipped) rather than any real reference. See
[docs/appstore.md](docs/appstore.md).

- `shell/store/` — `store.{h,cpp}` (the window, built entirely from
  existing `PanelList`/`PanelButton`/`pl_progress_paint` widgets),
  `storeclient.{h,cpp}` (catalog loading, helper invocation).
- `assets/store/catalog.json` — 20 curated apps across 6 categories plus
  Featured; shipped read-only, installed to `$datadir/zacos9/store/`.
- `appstore/` — `zacos9-appstore-helper` (root, through pkexec;
  validates every package name — Debian's own charset, never starting
  with `-` — *before* its root check, so the check runs regardless of
  caller) plus the matching polkit `.policy`/`.rules`, mirroring
  `sharing/`'s passwordless-for-sudo-group pattern (no polkit agent runs
  on this desktop to prompt for a password).
- An installed item's icon is the real one: matched by package against
  `appList()`'s `AppEntry::origin` (this session's earlier
  `dpkg:<package>` origin lookup), reusing the Finder's own icon
  resolution rather than duplicating it.
- Deliberately indeterminate progress (a sweep, not a percentage):
  parsing apt's own output for real progress isn't stable across
  versions; showing a possibly-wrong number seemed worse than an honest
  "working."
- `"Software"` added to `share/applications/` (so it is also discoverable
  from the Finder's own Applications folder, Categories=System) and to
  the Apple menu (`shell/menubar/launch.c`).

Verified end to end against the real system, not just `meson test`:
opened from the Apple menu; browsed every category; selected Calculator,
clicked Install, watched the real `apt-get install galculator` run
through the helper, confirmed installed both on screen ("Installed.",
the real calculator icon) and via `dpkg-query` outside the app; clicked
Remove, confirmed the alert names the package and offers Remove/Cancel,
confirmed removal both on screen and via `dpkg-query` — nothing left
installed afterward.

---

**Virtual Macintosh filesystem** (built, tested, not yet committed).

The Finder shows a startup disk holding System Folder, Applications and
Documents instead of Debian's Unix hierarchy. Nothing on disk is moved,
renamed or hidden; no FUSE mount. See [docs/vfs.md](docs/vfs.md).

- `shell/finder/vfs.{h,cpp}` — the node model and its JSON registry at
  `~/.local/share/zacos9/finder/vfs.json` (stable ids, display names,
  icons, backing paths, visibility, kinds, migration by `version`).
- `shell/finder/appdb.{h,cpp}` — installed applications through GIO:
  XDG precedence, `Hidden`/`NoDisplay`/`OnlyShowIn`/`NotShowIn`/`TryExec`
  (and GIO's own rule that a plain `Exec` naming a program that can't be
  found is dropped too), Desktop Actions, launching with full
  desktop-entry semantics (`Exec` is never handed to a shell), real
  application icons via `QIcon::fromTheme`, and package/version origin
  via `dpkg` (never guessed for Flatpak, read from `X-Flatpak` instead).
- `lib/draw.{h,c}`: `pl_image_blend`, real alpha compositing for a
  resolved icon (the compiled icon set stays plain on/off alpha).
- Hide-not-uninstall: Move To Trash on an application's folder asks for
  confirmation, then hides it via the same override mechanism a rename
  uses — never touches dpkg. **Special > Show All Applications** (new
  menu item, `shell/menubar/menus.c`) reverses it.
- Integrated at the existing chokepoints only: `listFolder()`,
  `openSelection()`, `displayName()`, the desktop's disk item,
  `FolderWindow`'s path handling, `LabelEditor::commit()`,
  `moveSelectionToTrash()`, `fileops`, `paintIconItem()`.
- Two new icons (System Folder with the sparkle, Control Panels with
  sliders) in `assets/icons/platinum-icons.picon`.
- `libglib2.0-dev` added to Build-Depends; `qt6-svg-plugins` added to
  Depends (SVG-only icons need the plugin at runtime) in `debian/control`.
- Deliberately not done: a Version/Package row in Get Info — that
  window is pixel-measured from a real Mac OS 9 screenshot, which has no
  such row; the data is there (`AppEntry::version`/`origin`) for when a
  reference screenshot justifies adding one.

Verified on screen, through real menu/keyboard interaction (not just
`meson test`): the disk's exactly three folders; Applications listing
one folder per installed application with its real resolved icon (a
Desktop Action keeps the generic document icon); Mousepad launching from
its folder; the System Folder's five folders; `showUnixVolume`'s second
"Unix" disk; and the full hide/confirm/restore round trip — Move To
Trash on an app folder, the confirmation alert naming it, the folder
disappearing from the open Applications window live, then Special > Show
All Applications bringing it back.

## Open issues / known gaps

- **`smbd` crashes on start in this dev container** (`core-dump`,
  signal ABRT, immediately after "ready to serve connections") — a
  pre-existing environment issue, not investigated (not this session's
  AFP/discovery work, and not something `zacos9-sharing-helper`'s
  config generation can cause). Blocks live verification of real SMB
  serving *and*, as a side effect, real SMB mounting (there was nothing
  reachable to mount from). Worth a real investigation before relying
  on SMB in this project at all.
- No SMB share-list dialog (see `docs/network.md`); no automatic
  retry/backoff in the Network Browser if `avahi-daemon` itself isn't
  running yet when it scans.
- AFP connects measured at roughly ten seconds end to end through the
  GUI in this dev container, versus well under one second for the same
  command run directly from a shell — almost certainly a WSL networking
  quirk (see `docs/network.md`), not a logic bug (the fix for the
  actual bug alongside this, a `QProcess::waitForFinished()` deadlock,
  is already in). Expect this to be faster on a real installed system;
  not confirmed either way yet.
- Network volumes can't be dragged (inherited, not introduced here: the
  startup disk itself already couldn't be, with its own pre-existing
  TODO) — ⌘Y or File > Put Away is the way to eject one.
- The Software window has no search and no batch install (checkboxes +
  one combined apt transaction, closer to Mac OS 9's own CD installer);
  both left out deliberately, see `docs/appstore.md`'s "deliberately
  left out" section. No "Check for Updates" either — `dpkg-query` says
  installed or not, not whether a newer version exists.
- `debian/control` still needs `fuse3`, `libfuse3-dev`, `libgcrypt20-dev`
  for `zacos9-afp` (left alone: separate feature — it's the same
  binary the Network Browser now drives, so this is worth picking up
  alongside it next).
- ISO has not been rebuilt since the silent-boot commit; it is missing
  file sharing, Get Info Sharing, the AFP client and the virtual filesystem.
- Long icon labels overlap slightly in icon view (seen with "Mousepad
  Preferences"); pre-existing `placeIcons` behaviour, not investigated.
- Get Info on a *curated* folder (System Folder, Applications, an
  application's folder) opens nothing, because `InfoWindow` reads a file
  and those stand for none. Applications and `backed` folders do work —
  they show their desktop entry / real directory. A Get Info of their own
  is the follow-up, and the natural place to eventually show version/origin.
- Resolved icons are kept in memory only (no disk cache); cheap enough in
  practice (one theme lookup per newly-discovered application) that this
  wasn't worth the added complexity, but worth revisiting if a system
  with hundreds of installed GUI apps makes refresh noticeably slow.
- `scripts/snapshot.sh` **was** suspected broken (its `x11-namespace.sh`
  re-exec appeared to leave UI tests talking to the wrong
  `WAYLAND_DISPLAY`) — re-tested this session and it now works correctly
  end-to-end. The earlier failure looks like the stale-WSLg-bridge class
  of issue this project already knows about (blank/stuck windows after
  sleep or a monitor change — `wsl --shutdown` fixes it), not a real bug
  in the script. If it recurs, shut down WSL first before concluding the
  harness is broken.

## Next steps (priority order)

1. **Investigate the `smbd` crash** in this dev container — blocks real
   verification of both SMB serving and SMB mounting.
2. **`debian/control`** — add `zacos9-afp`'s own dependencies
   (`fuse3`, `libfuse3-dev`, `libgcrypt20-dev`).
3. **An SMB share-list dialog**, once a working SMB server is available
   to test anonymous root-browsing against reliably (see
   `docs/network.md`).
4. **ISO rebuild** — once the above are in; it is missing every feature
   built since the silent-boot commit (file sharing, Get Info Sharing,
   the AFP client, the virtual filesystem, Software, the Network
   Browser).

## Test servers (local)

| Server | Address | Credentials |
|---|---|---|
| Netatalk (this machine) | `127.0.0.1:548` | `platest` / `Sesame-42`, volume `platest's home` |
| Mac OS 9 (Classic) | `127.0.0.1:5548` | `^Mac User` / `macpass`, volume `Macintosh HD` |

## Architecture cheat-sheet

See `CLAUDE.md` for the full map. Key paths:
- Finder: `shell/finder/` (virtual filesystem: `vfs.cpp`, `appdb.cpp`)
- Control panels: `shell/panels/`
- Software window: `shell/store/`, helper in `appstore/`
- Network Browser: `shell/network/`
- AFP client: `network/afp/`
- File sharing helper: `sharing/zacos9-sharing-helper`
- Tests: `meson test` (unit), `tests/ui/*.sh` (screenshots),
  `tests/sharing/afp-client.sh`

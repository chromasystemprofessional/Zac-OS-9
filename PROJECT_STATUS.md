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

## Current work

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

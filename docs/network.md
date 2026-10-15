# The Network Browser

Connecting to other computers: a classic Mac over AFP (`network/afp`,
already built — this is the GUI it was written for), a Windows or modern
Mac share over SMB (through gvfs, Debian's own client). Not pixel-
measured: Mac OS 9 originals were photographed in an earlier session but
the screenshots lived only in that session's scratchpad, not the repo,
and were gone by the time this was built. Laid out from the HIG controls
the rest of the control panels already use, the same honest approach
`shell/panels/filesharing.cpp` itself documents in its own TODO.

## Architecture

| Piece | Path | What |
|---|---|---|
| The window | `shell/network/netbrowser.{h,cpp}` | The server list, Connect to Server, and the whole login → volumes → mount sequence. |
| Finding servers | `shell/network/discovery.{h,cpp}` | Bonjour (`avahi-browse`) and our own WS-Discovery probe. |
| AFP | `shell/network/afpclient.{h,cpp}` | Wraps `zacos9-afp info`/`volumes`/`mount`. |
| SMB | `shell/network/smbclient.{h,cpp}` | Wraps `gio mount`. |
| What's mounted | `shell/network/netvolumes.{h,cpp}` | Read from the filesystem itself (`/proc/self/mountinfo`, gvfs's runtime directory) — no registry of our own to drift from reality or blocking `findmnt` child process. |
| Dialogs | `connectdialog.*`, `logindialog.*`, `volumedialog.*` | Address, Guest/Registered User + Name/Password (+ Share for SMB), the AFP volume list. |
| Desktop integration | `shell/finder/desktop.{h,cpp}` | Mounted volumes as disk icons, polled every 3 s for ordinary mount changes; AFP connection loss notifies Finder immediately. |
| Ejecting | `shell/finder/finder.cpp` (`putAway`) | ⌘Y ejects a network volume instead of trying to move a file. |

## Discovery

**Bonjour**, through two concurrent, service-specific commands:
`avahi-browse -k -p -r -t _afpovertcp._tcp` and
`avahi-browse -k -p -r -t _smb._tcp`, not linking avahi-client directly.
Each query is bounded by the same scan timeout, and unrelated services cannot
delay AFP resolution. Three things about it only became clear by actually
running it against this project's own Netatalk test server:

- `avahi-browse` takes **at most one service type** as an argument.
  Given two (`_afpovertcp._tcp` and `_smb._tcp`), it refuses outright
  with "Too many arguments" and browses nothing — silently, from the
  caller's point of view, since the failure is in argument parsing, not
  in finding anything. The two service types are queried separately.
- Browsing **every** service (`-a`) can take longer because Avahi resolves
  unrelated records too. The Network Browser's 1.5-second scan budget could
  expire after AFP's unresolved `+` records arrived but before its resolved
  `=` records, making an available AFP server disappear from the list. The
  focused AFP/SMB queries now run concurrently under one shared deadline.
- Without `-k`, avahi-browse substitutes a friendly name for the service
  type even in parseable (`-p`) output — `_afpovertcp._tcp` comes back
  as `Apple File Sharing`. `-k` turns that off. (`test_discovery.cpp`
  keeps a real captured line of each form, so a regression here fails a
  test, not just a live check nobody happens to run.)
- The same server, announced on more than one interface, is one
  computer, not two: `discoverServers()` dedupes by name after merging
  Bonjour and WS-Discovery, not just by address.
- A server can be announced over **IPv6 alone** for a while, even with
  an IPv4 address (seen live on 2026-10-10: two ZacOS computers' AFP
  records came over IPv6 only, then IPv4 again, and the Network Browser
  missed them meanwhile, as it took IPv4 records only). Such a server is
  now listed by its `.local` host name, which the system resolves
  (`mdns4_minimal`); a server with an IPv4 record is still listed by that
  address, once (`parseAvahiBrowse`).
- Names are UTF-8, escaped a byte at a time (`Adam\226\128\153s` is
  "Adam’s"): they're decoded as UTF-8, not one character per byte.

Netatalk needs nothing from this project to be found: once
`avahi-daemon` is running (the File Sharing control panel's helper
already starts it when AFP sharing is turned on), Netatalk announces
itself through avahi's own Bonjour-compatibility library, under the
server's configured name — confirmed live. Samba's own avahi support
could not be confirmed the same way: `smbd` crashed on startup in the
container this was built in (`core-dump`, `abrt`; not investigated
further, as it is unrelated to this feature — see the AFP work in an
earlier session for `smbd`'s general status), so
`sharing/zacos9-sharing-helper` still writes an explicit
`_smb._tcp` avahi service file of its own when SMB sharing is on,
kept rather than assumed redundant.

**Windows computers**, through a WS-Discovery Probe multicast to
`239.255.255.250:3702` (`shell/network/discovery.cpp`): Debian ships no
query tool for this (`wsdd2` only *answers* other computers' probes).
The protocol mechanics were verified live — probing from inside this
dev container, and confirming Windows' own `FDResPub` service is active
and that this container's own `wsdd2` responder answers a probe sent to
it (`CHROMASYSTEMPRO-SERVER` showed up, deduplicated from two interfaces
answering) — but **no second, independent Windows computer was ever on
the dev network to probe**, so finding one that isn't also this
container's own services has not been confirmed. The implementation
follows the spec as published; if it doesn't find a real Windows
computer on a real network, that is the first thing to check against a
packet capture.

## AFP: login, volumes, mount

Select one network disk on the desktop and choose **Special → Disconnect
Network Drive** to disconnect AFP or SMB. For a mounted local disk, including
USB storage, choose **Special → Unmount Disk** before unplugging it. Each item
is enabled only for the appropriate single selected disk; startup disk and
ordinary folders cannot be unmounted. Existing Put Away remains available.
Successful network disconnects remove the desktop icon immediately and do
not show an unexpected-disconnection warning. Failed disconnects and local
unmount errors are reported without pretending the operation succeeded.
Installed live on 2026-10-08; Finder and the menu bar were restarted with user
approval. Offscreen tests cover menu enablement, USB helper routing, successful
SMB disconnect, and a refused SMB unmount that misleadingly exits zero.

All three map straight onto `zacos9-afp`'s own commands (`info`,
`volumes`, `mount`), already built with this GUI in mind — see its own
usage comment in `network/afp/main.c`.

### Connection loss

The AFP daemon monitors idle sockets as well as active requests. TCP EOF/reset,
a DSI session-close, or AFP session-closed/server-going-down errors terminate
the failed FUSE session and unmount it. After unmounting, the daemon sends
`afp-disconnected <hex-encoded mount path>` over the existing per-display
Finder socket. Finder immediately removes that disk icon and opens a Platinum
**Server Disconnected** dialog identifying the disk, suggesting reconnection,
and warning that unsaved changes may have been lost. This does not wait for
the Desktop's three-second volume poll. Normal Put Away/eject remains silent;
ordinary file errors, including permissions, missing files and file-level I/O
errors, do not disconnect the whole volume.

Explicit disconnects are detected promptly, including while idle. Silent
network outages cannot be detected instantaneously: TCP keepalive and
unacknowledged-data timeouts are configured to detect a lost peer in roughly
20 seconds. A connected server that stops replying to AFP requests still has
the existing 90-second receive timeout. The daemon logs failures to configure
monitoring or deliver the Finder notification. No automatic reconnect or
replacement mount is attempted.

Installed live on 2026-10-08 and restarted Finder with user approval.
Existing AFP mounts keep their running client, so eject and remount once to
activate the new disconnect handling for those disks.

`meson test -C build afp-disconnect finder-desktop` tests real FUSE mounts
against an isolated local mock AFP server (no live shares or credentials):
idle EOF/reset/session-close, active-request failure, AFP terminal session
errors, background daemon teardown and silent normal eject. It also checks
that ordinary file errors preserve the mount and that Finder's real socket
removes only the failed disk before showing the dialog. The FUSE test skips
explicitly when mounts are not permitted.

The login dialog's **Guest / Registered User** choice, and whether to
warn before sending a cleartext password, both come from `info` first:
its `guest` line and its `uam` list (no secure UAM, only `Cleartxt
Passwrd`, means the warning). A wrong password (`zacos9-afp`'s own
exit code 2, its documented convention for a refused login) re-shows the
login dialog with the reason rather than failing outright.

### Two real bugs, found only by actually connecting

- **`QProcess::waitForFinished()` deadlocked.** `zacos9-afp volumes`
  completed its exchange with the server in full — confirmed from
  Netatalk's own log: a clean login, then "AFP logout", a moment
  later — and exited, but `waitForFinished()` in the GUI process never
  returned; it hung until its own timeout killed it. The rest of this
  project already avoids `waitForFinished()` for exactly this reason
  (see `runSharingHelper` in `sharingclient.cpp`, written in an earlier
  session); this file didn't, until it hit the same problem. Fixed by
  switching to the same `processEvents()`-loop pattern.
  The underlying exchange itself was also found to take roughly ten
  seconds end to end when run this way, against well under a second for
  the identical command run directly from a shell — almost certainly a
  WSL networking quirk (this project's memory already tracks a class of
  WSL-specific issues), not investigated further. **Expect this to be
  much faster on a real installed system**, and treat a multi-second
  wait after Connect as normal until that's confirmed one way or the
  other on real hardware.
- **The mount command's own `--` was rejected.** `afpMount` built its
  shell command with a `--` before the server address, the usual
  end-of-options marker — except `zacos9-afp`'s own argument parser
  (`network/afp/main.c`) doesn't recognize that convention: it reads
  `--` as an unrecognized flag of its own and refuses with its usage
  message. Removing it was enough; the parser already stops consuming
  `--something` flags once it reaches a plain argument.

Both were only found by actually connecting end to end against this
project's own Netatalk test server and reading what came back — neither
would have been caught by code review alone.

## SMB

Through `gio mount`, not a client of our own. One real finding changed
the design, and one more changed the error handling:

- **There is no share-list dialog for SMB**, unlike AFP's. Browsing an
  SMB server's share list anonymously needs its root mounted first
  (`gio mount smb://server/`), and that behaved inconsistently testing
  against this project's own (admittedly broken — `smbd` wouldn't stay
  up) test instance. Rather than ship something unverified, the login
  dialog for an SMB server asks for the share name directly, the way
  Windows' own "Map Network Drive" asks for `\\server\share` rather than
  browsing. AFP keeps its volume list, because `zacos9-afp volumes`
  (backed by AFP's `FPGetSrvrParms`) was verified reliable.
- **`gio mount`'s own exit code is 0 whether or not the mount actually
  worked** — tested live against a server refusing the connection
  (smbd down in this container): a clear "Failed to mount Windows
  share: Invalid argument" on stderr, exit code 0 regardless.
  `smbMount()` goes by whether anything was written to stderr, not the
  exit code.
- Guest connects with `gio mount -a` (no prompting at all, confirmed
  live). A named user answers `gio mount`'s own interactive prompts on
  its stdin — user, then domain (left blank, taking the server's
  default workgroup), then password, one line each, in that order
  (there is no flag for this; the order was found by actually running
  it and watching what it asked for, piping blank lines in to see each
  prompt in turn).

**What could not be verified live**: an actual successful SMB mount.
`smbd` would not stay running in this container (`core-dump` on start,
unrelated to this feature), and no other SMB server was reachable on the
dev network either. `smbMount()`/`smbUnmount()` follow gvfs's documented
conventions (the `smb-share:server=HOST,share=SHARE` mount-name format
`netvolumes.cpp` parses is the standard one gvfs has used for years),
but the success path — a real share actually mounting, appearing on the
desktop, surviving Put Away — has only been exercised for AFP. Test
against a real SMB server before relying on this.

## What's mounted, and ejecting it

`netvolumes.cpp` asks the filesystem, not a registry of its own:
`findmnt -t fuse.afp` for AFP, gvfs's own runtime directory
(`$XDG_RUNTIME_DIR/gvfs/smb-share:...`) for SMB. A volume unmounted by
hand at a terminal disappears from the desktop on the next poll (every
3 s — nothing posts an event for a plain `fusermount3 -u`, the same
trade `FileSharingPanel` already makes polling who's connected, there
every 5 s), exactly as if Put Away had done it, because as far as the
Finder is concerned nothing is different.

An AFP volume mounts at `~/.local/share/zacos9/mounts/<server>/<id>/<name>`
(the directory's own name is the disk's name — no separate record of
what's what). `<id>` is 16 hex digits of a SHA-256 of the exact server
(lowercased) and volume names, so volumes whose names make the same
folder name — "a/b" and "a-b", or a home volume shown as "adam" beside a
volume called "adam" — never share a mount point (`afpMountPointIn`). The name is the shared folder's, as on Mac OS 9: never
numbered, and two servers' volumes of the same name sit side by side on
the desktop. Netatalk's default home volume name, "NAME's home", is shown
as the folder NAME when you logged in as NAME (`afpVolumeDisplayName`);
ZacOS's own File Sharing names home volumes after the folder anyway
(`home name = $u`). The same volume always gets the same place, so it
comes back where its icon was. Before mounting, empty folders left by
earlier connections (a logout or a dropped server skips the cleanup that
follows an unmount) are removed, a dead mount in the way is lazily
unmounted, and a volume that's already mounted isn't mounted again —
but only if the mount really is that volume: its source,
`afp://SERVER/VOLUME` as `zacos9-afp` writes it (parsed as plain text,
not a URL, so a port, `#` or `?` survives), must match
(`afpMountIs`/`afpMountAmong`). A volume still mounted where 0.1.48–0.1.50
put it (`mounts/<server>/<name>`) is found there; a different volume
mounted in a volume's place is neither reused nor mounted over — the
Network Browser says so instead. The cleanup removes empty folders down
to `<server>/<id>/<name>`, and never looks inside a mount, or another
filesystem mounted there. (Upgrading from 0.1.50, a volume's desktop icon
loses its remembered position once, as its mount point moves.)
(Until 0.1.47 the folder was `mounts/<name>`, numbered " (2)", " (3)" ...
whenever a leftover folder had the name, so reconnecting piled them up.)
Mounting and ejecting were verified live end to end: mounted (confirmed with `df`, a
real write), appeared on the desktop as a disk icon within the next
poll, ejected with ⌘Y (confirmed gone from both the desktop and
`findmnt`).

**Network volumes can't be dragged yet** — not something this added:
`Desktop`'s drag code already excluded every "fixed" item (the startup
disk included) from being dragged at all, with its own pre-existing
`TODO: dragging the disk to the Trash ejects it on a Mac`. Network
volumes inherited that gap rather than closing it; ⌘Y (or File > Put
Away) is the way to eject one for now.

## Tests

`shell/network/tests/test_discovery.cpp` (`meson test discovery`) checks
`avahi-browse` line parsing against real captured output (including the
friendly-name substitution bug above, so a regression there fails a
test) and WS-Discovery `ComputerName` extraction, without needing a live
network.

`tests/sharing/afp-client.sh` (an earlier session) already covers
`zacos9-afp` itself. This session's verification of the GUI on top of
it was manual and end-to-end rather than automated — screenshots and
`findmnt`/`dpkg`-style checks against this project's own test server,
not a script committed to the repo. A `tests/ui/` script scripting the
full discover → connect → log in → choose a volume → mount → appear on
the desktop → eject sequence (`scripts/snapshot.sh` itself works fine;
see `PROJECT_STATUS.md`) is a reasonable follow-up.

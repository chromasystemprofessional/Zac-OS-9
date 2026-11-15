# Classic Open Helper

This is the guest half of the direct-launch prototype for milestone 1 of the
[seamless Classic plan](../../docs/classic-seamless.md). It is an original
Retro68 application: no Apple software, headers or assets are used or shipped.

It runs in the Mac's `System Folder:Startup Items`, watches the shared Unix
volume for an open request, and launches the requested Classic application with
the Process Manager. The ZacOS Finder writes that request when a Classic
application is double-clicked.

## What works

- A new Classic session started from a Classic application in the shared home
  opens that application once the Mac is ready.
- A running session receives one request, opens the application, and writes
  an acknowledgement.
- A second `--open` in the same session is refused immediately: this prototype
  supports one direct open per Classic session. Quit Classic and start it again
  to open another application.
- The host waits for that acknowledgement. If the helper is missing, or does
  not take the request, `zacos9-classic --open` fails after five seconds
  instead of claiming success.

## Known limit

**Current decision:** one direct open per Classic session. The host refuses a
second request before writing it, and asks the user to restart Classic. This is
a deliberate limit, not the milestone 1 gate: two applications in one session
are **not** supported.

The guest File Manager caches the ExtFS directory after its first read. The
helper can therefore open the **first** request in a session; a second request
would be written but not noticed by the guest.

The shared-folder queue is not a viable transport for later requests. On the
Mac OS 9.0.4 test guest, pre-created request slots are visible at boot, but
after boot the File Manager keeps returning its cached view of the volume:
new sizes, modification dates and names stay invisible even after
`PBGetCatInfo`, `FSpOpenDF` and `GetEOF`. Do not build production host windows
on this queue.

The serial port is also blocked on that guest. SheepShaver's output driver
(`.AOut`) returns `openErr` (-23) because it needs `DriverServicesLib`; the Mac
OS 9 install used for testing does not provide that library, so the serial
bridge process never starts. A TCP bridge over the emulator's NAT is the next
candidate. On a repaired Mac OS 9 guest, a prototype connected to
`10.0.2.2`, launched SimpleText, and displayed its window. A second request
still timed out: after the app switch, MacTCP's synchronous receive and close
calls stopped returning. A permanent asynchronous receive using a raw
completion routine crashed the guest. The next attempt should use Open
Transport or a correctly built asynchronous completion UPP, not a blocking
MacTCP receive loop.

A Finder `Open Documents` Apple Event was also tried, to keep the helper out of
the application launch. The Finder rejected both FSSpec and alias items for
applications on the ExtFS volume with error -903, so that route did not help.
The Open Transport probe linked, but `OTOpenEndpointPriv` crashed with a
hand-written prototype; the real Universal Interfaces 3.x headers are needed
to get that entry point's signature right.

Universal Interfaces 3.4 was then mounted, and the probe builds against the real
headers and the PPC Open Transport glue. The repaired test guest still does not
launch it: that Mac OS 9 install lacks the `OTClientLib`/`OTClientUtilLib`
client libraries. A clean Mac OS 9 installation is required before Open
Transport can be used.

A clean Mac OS 9 installation was then made on a fresh disk. It boots normally
and opens SimpleText, but the MacTCP bridge still times out on the second
request there, so the post-app-switch receive failure is not disk corruption.
The real-header Open Transport probe also does not run from Startup Items on
that install, so Open Transport launch/library support is still unresolved.
Launching it manually from Applications left it frontmost with no log entry and
no network connection, so it hangs before `InitOpenTransport` returns.

The prototype also only opens applications that are files in the shared home
folder, with plain ASCII names and no colon. Applications inside a guest disk
image are not reachable by this path; open those inside the Mac for now.

## Build

Use Retro68's PowerPC classic toolchain with the Multiversal Interfaces, as
described in [the rendering spike](../seamless/README.md):

```sh
cmake -S emulation/classic-open -B /tmp/classic-open-build \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DRETRO68_ROOT=/path/to/toolchain
cmake --build /tmp/classic-open-build
python3 emulation/classic-open/patch_size.py \
  /tmp/classic-open-build/.rsrc/ClassicOpenHelper.APPL
```

`patch_size.py` sets the SIZE resource's `canBackground` flag. Retro68 does not
set it, so without this step the helper is suspended as soon as another Classic
application comes forward. The
generated `.APPL`, `.rsrc/ClassicOpenHelper.APPL` and
`.finf/ClassicOpenHelper.APPL` are what SheepShaver's ExtFS reads. Copy all
three to the Mac (the shared Unix volume is easiest), then drag the
application into `System Folder:Startup Items` and restart the Mac. No Apple
files are included.

To remove it, quit it from its **Classic Open** menu (in the Application menu)
or remove it from Startup Items and restart.

## Protocol

The host (`zacos9-classic --open APP`) writes one request in the Mac's
`Unix:zacos9-classic:requests` folder. Every request uses a new name because the
guest caches names after use; the sequence file records the last number
issued.

```text
requests/request.<n>.req
  version=1
  id=<n>
  path=<colon-separated path below the Unix volume>

acks/ack-<n>
  version=1
  id=<n>
  status=ok | status=error
  code=<Mac error, for status=error>

sequence
  <last n issued>
```

A new Classic session resets the queue. `acks/helper-ready` is written by the
helper when it finds the queue. The helper validates the version, id and path,
requires the target's Finder type to be `APPL`, and launches it with
`LaunchApplication`.

## Current host flow

- `shell/classic/zacos9-classic --open APP` maps `APP` below `$HOME` to the Mac
  path and keeps one emulator session (a pid file under `$XDG_RUNTIME_DIR`).
- `shell/finder/finder.cpp` passes a double-clicked `PL_ICON_CLASSIC` item to
  that launcher.
- A first request in a new session opens after boot. A first request in a
  running session is acknowledged. A second request in that session currently
  fails as described above.

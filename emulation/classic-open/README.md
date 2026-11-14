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
- A running session receives its first request, opens the application, and
  writes an acknowledgement.
- The host waits for that acknowledgement. If the helper is missing, or does
  not take the request, `zacos9-classic --open` fails after five seconds
  instead of claiming success.

## Known limit

The guest File Manager caches the ExtFS directory after its first read. The
helper can therefore open the **first** request in a session; a second request
in the same session is written but not noticed by the guest. The host now waits
for the current acknowledgement and reports the failure, but the milestone 1
gate (two applications in one session) is **not** met.

The next transport should be the emulator's serial port (`SheepShaver
--seriala`/`--serialb`, with a pty) or another narrow bridge. Do not build
production host windows on the shared-folder queue.

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

`patch_size.py` clears the `cannotBackground` flag if the Retro68 template set
it; the current build is already background-capable, so it is a no-op. The
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

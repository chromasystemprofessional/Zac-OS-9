# Classic Open Helper

This is the guest half of direct application launching for milestone 1 of the
[seamless Classic plan](../../docs/classic-seamless.md). It is an original
Retro68 application: no Apple software, headers or assets are used or shipped.

It runs in the Mac's `System Folder:Startup Items`, polls the patched
emulator's Classic bridge, and launches the requested Classic application with
the Process Manager. The host writes the request when a Classic application is
double-clicked in ZacOS. The emulator, not the guest, reads the request queue
and writes the acknowledgement, so the guest needs no network service and is
not affected by the guest File Manager's ExtFS directory cache.

The helper requires the patched SheepShaver built by
`emulation/build-emulators.sh`. An unpatched emulator does not implement the
bridge opcode and treats the unknown selector as a fatal error, so install the
patched emulator before this helper. Basilisk II is not supported yet.

## What works

- A new Classic session started from a Classic application in the shared home
  opens that application once the Mac is ready.
- A running session accepts further opens, one at a time. Two applications
  have been opened in one session (SimpleText, then SimpleSound).
- The host waits for each acknowledgement. If the helper is missing, or does
  not take the request, `zacos9-classic --open` fails after five seconds
  instead of claiming success.
- The helper keeps running in the background after another application comes
  forward (`patch_size.py` sets the SIZE resource's `canBackground` flag).

## Limits

- Only applications that are files below the shared home folder can be opened
  directly. They must have Finder type `APPL`, plain ASCII names, no colon, and
  a path of at most 199 bytes. Applications inside guest disk images are not
  reachable this way yet.
- Documents are not passed to the application yet; this launcher opens an
  application, not a document.
- The helper is not installed automatically. Copy it into Startup Items as
  described below.
- Independent window export, input forwarding and the **Show Classic Desktop**
  fallback are milestone 2 work and are not implemented here.

Earlier prototypes used the shared-folder queue directly, a serial port,
MacTCP, Open Transport and Finder Apple Events. They are not part of this
helper: the shared folder was cached by the guest, the serial driver needed a
library the test install lacked, and MacTCP stopped receiving after an
application switch. The emulator bridge avoids guest networking entirely.

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

Add `-DCLASSIC_OPEN_DEBUG=ON` to write `ClassicOpenDebug.txt` on the Unix
volume for bridge and launch diagnostics. `patch_size.py` sets the SIZE
resource's `canBackground` flag; Retro68 does not set it, so without this step
the helper is suspended as soon as another Classic application comes forward.

The generated `.APPL`, `.rsrc/ClassicOpenHelper.APPL` and
`.finf/ClassicOpenHelper.APPL` are what SheepShaver's ExtFS reads. Copy all
three to the Mac (the shared Unix volume is easiest), then drag the
application into `System Folder:Startup Items` and restart the Mac. No Apple
files are included.

To remove it, quit it from its **Classic Open** menu (in the Application menu)
or remove it from Startup Items and restart.

## Protocol

The host (`zacos9-classic --open APP`) writes a request below
`$HOME/zacos9-classic`; the emulator is told the same directory through
`ZACOS9_CLASSIC_QUEUE`. A request is written to a temporary name and renamed,
so the emulator never sees a partial file.

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

A new Classic session resets the queue. The emulator bridge picks the
lowest-numbered request, gives it to the helper, then writes the
acknowledgement atomically and removes the request. The host serialises
requests: a second open waits for the previous acknowledgement before writing
the next request, so only one is outstanding.

The helper calls emulator selector 53 with the instruction word `0x18000038`
and a block pointer in A0:

```text
+0    command       1 poll, 2 acknowledge
+4    result        1 when a request or acknowledgement was handled
+8    request id
+12   path          200 bytes including the terminator
+212  ack status    0 ok, 1 error
+216  ack code      Mac OSError
```

For a poll, the emulator validates the request version, numeric id and path,
copies the path into the block, and returns 1. The helper resolves each path
component on the Unix volume, requires Finder type `APPL`, launches the
application with `LaunchApplication`, activates it, and sends an acknowledge
command with the result. The guest never sees the queue directory, so the
ExtFS cache cannot hide later requests.

## Current host flow

- `shell/classic/zacos9-classic --open APP` maps `APP` below `$HOME` to the Mac
  path and keeps one emulator session (a pid file under `$XDG_RUNTIME_DIR`).
- `shell/finder/finder.cpp` passes a double-clicked `PL_ICON_CLASSIC` item to
  that launcher.
- A request in a new session opens after boot. Further requests in the running
  session are queued and acknowledged one at a time.

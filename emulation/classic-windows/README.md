# Classic Window Agent

This is the guest half of milestone 2 of the
[seamless Classic plan](../../docs/classic-seamless.md): a background Classic
application that discovers windows from every process and keeps an independent
pixel buffer for each one. It is original Retro68 code; no Apple software,
headers, fonts or assets are shipped.

The agent runs from `System Folder:Startup Items`. It installs native
InterfaceLib transition-vector hooks for the Window Manager and QuickDraw
calls that ordinary applications use, identifies the window that owns each
draw, and replays the operation into a per-window color GWorld before calling
the original function. A window that is fully covered by another window still
has current pixels.

This is a development slice. It writes an inventory and PPM frames to the
shared Unix volume; a host window frontend and input forwarding are not
implemented yet.

## What works

- Discovers document windows in separate processes. Verified with SimpleText
  and Mac OS 9's Graphing Calculator 1.1 in one Classic session.
- Captures direct 16-bit and 32-bit color windows. SimpleText captured typed
  text exactly; the Graphing Calculator window captured its labels, axes and
  button.
- Tracks window title, owner process, process name and signature, window kind,
  visibility, depth, bounds and a stable session serial.
- Invalidates a capture when the window is disposed.
- Seeds a new capture with the window's background color, so document windows
  whose initial background is painted by the Window Manager do not start black.
- Replays `PaintRect`, `EraseRect`, `DrawText`, `StdText`, `FrameRect`,
  `LineTo`, `CopyBits` (when the destination is the window) and `ScrollRect`.
  `StdText` is optional; the agent still starts if InterfaceLib does not export
  it.

## Host-visible files

The agent writes these files below the shared Unix volume (the host's home
folder) every two seconds when a capture changed:

```text
ClassicWindows.txt          one line per live window
ZacWin-<serial>.txt         metadata for one window
ZacWin-<serial>.ppm         P6 RGB frame, version-1 comment
ClassicWindowsError.txt     only after a setup or export error
```

`ClassicWindows.txt` starts with `version=1`, `count`, `hooks`, `error`,
counters and a `capture` count. Each `window=` line has `serial`, `psn`,
`app`, `sig`, `kind`, `visible`, `color`, `depth`, `bounds`, `title`, `calls`,
`frame`, `capture`, `error`, replay operation counts and the last background
and clip values. The per-window metadata repeats the fields needed by a host
frontend.

The PPM header is:

```text
P6
# zacos9-classic-window 1 <serial> <frame>
<width> <height>
255
```

Frames are overwritten in place. The host must use `ClassicWindows.txt` to
ignore files from closed windows; the agent does not delete old frames.

## Limits

- Only direct 16-bit RGB555 and 32-bit ARGB color windows. Monochrome,
  indexed-color and custom `grafProcs` windows are listed but not captured.
- Windows must start at content coordinate 0,0, be at most 1024x768, and fit
  within eight simultaneous captures.
- The capture is seeded with `rgbBkColor`; a window whose background is a
  pattern, image or custom fill may initially show the wrong background until
  the application draws it.
- `FillCRect`, `FillRect`, `InvertRect`, `RGBForeColor`, `RGBBackColor` and
  other operations are not replayed yet. Applications that draw with them can
  be missing content; the inventory's `error` field records the last refused
  operation, and the host must treat such captures as incomplete.
- `CopyBits` is replayed only when the destination is the window. An offscreen
  source is used as-is; a screen-source alias is refused.
- Exporting a full PPM every two seconds is prototype transport, not the
  production frame path. It writes about 1 MB per 615x541 window per update.
- No input, close, resize, focus or menu forwarding. The host cannot yet
  activate or type into an exported window.
- A window that never draws after the agent starts is not discovered. Hooking
  window creation and activation is the next step.
- The agent is a Startup Item application, not an INIT. It uses the background
  `SIZE` flag and must stay loaded while its native vectors are installed. Do
  not force-quit it.

## Build

Use Retro68's PowerPC classic toolchain with the Multiversal Interfaces:

```sh
cmake -S emulation/classic-windows -B /tmp/classic-windows-build \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DRETRO68_ROOT=/path/to/toolchain
cmake --build /tmp/classic-windows-build
python3 emulation/classic-open/patch_size.py \
  /tmp/classic-windows-build/.rsrc/ClassicWindowAgent.APPL
```

The SIZE resource already sets `canBackground`; `patch_size.py` is a no-op for
this target but keeps the same installation check as Classic Open Helper.
`surface.h` is the bounded GWorld/export helper shared with the feasibility
spike.

Copy the generated `.APPL`, `.rsrc/ClassicWindowAgent.APPL` and
`.finf/ClassicWindowAgent.APPL` to the Mac, then drag the application into
`System Folder:Startup Items` and restart. The shared Unix volume is easiest.
No Apple files are included.

## Recorded result

In the disposable Mac OS 9.0.4 guest, with SimpleText opened from ZacOS and
Graphing Calculator opened from the shared volume:

```text
SimpleText.APPL   kind=8 visible=1 color=1 depth=32  bounds=0,0,615,541
                  title="untitled" capture=1
                  ops paint=0 erase=2 text=14 frame=0 line=12 bits=0 scroll=0
GraphingCalculator kind=8 visible=1 color=1 depth=32 bounds=0,0,634,552
                  title="Graphing Calculator 1.1" capture=1
                  ops paint=0 erase=0 text=6 frame=0 line=32 bits=1 scroll=0
```

The exported SimpleText frame contains the typed text `Hello Classic` at the
same pixel positions as the guest window. The Graphing Calculator frame
matches the empty graph window shown on the guest screen.

## Next

- Move frame and inventory transport onto the emulator bridge or shared memory
  instead of rewriting PPMs.
- Add a host frontend that creates ordinary ZacOS toplevels with ZacOS title
  bars and a **Show Classic Desktop** fallback.
- Add input forwarding, close/resize requests and window activation.
- Hook window creation/activation for windows that do not draw after startup.
- Handle `FillCRect`/`FillRect`, custom drawing procedures and indexed color,
  then broaden the compatibility matrix.

# Classic window rendering feasibility spike

This is milestone 0 of the [seamless Classic plan](../../docs/classic-seamless.md).
It is an experiment, not a production bridge, guest extension or seamless mode.
The current emulator and Classic launcher are unchanged.

## Source investigation

Inspected macemu revision `892eeb74ab9d70dfb034138a0b39057b14f275bc`
in a separate temporary checkout, not the excluded upstream/build directories.

- [SheepShaver video](https://github.com/kanjitalk755/macemu/blob/892eeb74ab9d70dfb034138a0b39057b14f275bc/SheepShaver/src/video.cpp)
  maintains `screen_base`, the guest display framebuffer.
- [Shared SDL backend](https://github.com/kanjitalk755/macemu/blob/892eeb74ab9d70dfb034138a0b39057b14f275bc/BasiliskII/src/SDL/video_sdl.cpp)
  presents that framebuffer, including the static-refresh path used without
  VOSF. Creating more SDL windows does not create missing guest pixels.
- [Native QuickDraw acceleration](https://github.com/kanjitalk755/macemu/blob/892eeb74ab9d70dfb034138a0b39057b14f275bc/SheepShaver/src/gfxaccel.cpp)
  has fill, blit, synchronization and other operation hooks. Dirty regions
  are reported when the destination is `screen_base`. Acceleration is
  conditional on `gfxaccel` and handles selected operations; this is not
  proof of complete, pre-clipping per-window rendering interception.
- [Native thunk definitions](https://github.com/kanjitalk755/macemu/blob/892eeb74ab9d70dfb034138a0b39057b14f275bc/SheepShaver/src/include/thunks.h)
  provide guest-callable native entry points. They are a candidate for a
  future private bridge, not an existing safe application/window protocol.

The guest experiments below establish bounded native monochrome interception
before visibility clipping. General drawing coverage without changing the
application, corrupting Window Manager regions or running Toolbox code from a
video thread remains a research question. The acceleration hooks alone do not
establish this.

## Original guest fixture

[probe/probe.c](probe/probe.c) builds a PowerPC application using Retro68 and
the open-source Multiversal Interfaces. It uses no Apple development assets.
The app creates two initially fully overlapping 320x192 monochrome QuickDraw
windows. Both patterns advance every six guest ticks, including the obscured
window's cooperative offscreen reference buffer.

- An in-process `QDProcs.rectProc` hook replays the window's ordinary
  `PaintRect`/`EraseRect` operations into a separate candidate port before
  calling `StdRect` on the original screen-backed port. Candidate pixels
  are not copied from the reference or calculated from the oracle.
  This controlled black/white rectangle experiment does not intercept
  other drawing operations, preserve arbitrary graphics state or install
  hooks in unmodified applications.
- **Probe > Pause** freezes the sequence for inspection.
- **Probe > Switch window** brings the other window forward.
- **Probe > Export snapshots** writes six named PBM files in the current
  guest working folder, replacing prior exports with the same names.
- Drag the guest title bars to test fully obscured, partial overlap and
  separated layouts. The standard grow region supports 64x64 to 640x384.
- Closing or quitting asks for confirmation; Cancel preserves the window.

The files are `ZacProbe-{0,1}-{reference,candidate,screen}.pbm`.
Export while both windows
are open. The reference files are drawn cooperatively by **our test app**;
the screen files are crops of the actual guest framebuffer. Offscreen
references are an oracle, not a solution for unmodified third-party apps.

The top eight rows encode the 32-bit sequence number; the rest is a changing
checkerboard unique to each window. PBM rows have no QuickDraw alignment
padding. Each header carries fixture version, source mode, window id, frame,
width and height. No timers export files automatically.

## Building the guest fixture

Use [Retro68](https://github.com/autc04/Retro68) with its default
**Multiversal Interfaces**, not redistributed Apple interfaces. For example:

```sh
cmake -S emulation/seamless/probe -B /tmp/classic-probe \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DRETRO68_ROOT=/path/to/toolchain
cmake --build /tmp/classic-probe
```

Guest code builds with `-Wall -Wextra -Werror`. Retro68 produces a MacBinary
application (`ClassicWindowProbe.bin`), an application-only HFS disk
(`ClassicWindowProbe.dsk`) and resource-fork-aware loose-file formats.
Do not copy a bare `.APPL` data fork and discard its resources.
The same build also produces `ClassicDrawingFixture` and `ClassicCaptureHelper`
in these formats; their roles and limitations are described below.

Validated with the public `ghcr.io/autc04/retro68` PowerPC toolchain,
GCC 16.1.0, using the toolchain layer:
`sha256:97cc7979af87ae2a4c9c5da1ddae896108a8508b0e617ed9d946ef4acadf3391`.
The layer was checksum verified and tools/dependencies extracted privately;
no Docker daemon, system package installation or Apple SDK was used.
The investigated Retro68 source revision was
`8d1e0da4cb8234ccc101ae961854085b09e87119`. The public image is not claimed to
be a reproducible build of that source revision; pin and rebuild the complete
toolchain before production distribution.

## Running the experiment

Use a disposable Mac OS 9.0.4 test setup with user-supplied ROM/system
software. Do not modify or attach a writable image already open in another
emulator. The generated HFS disk contains only our app, not Mac OS.

1. Copy the resource-preserving application to a dedicated guest test folder
   (preferably on the shared Unix volume), or attach the application-only HFS
   disk to a separate test session.
2. Launch inside the Mac. Keep both windows open, let the pattern advance,
   then pause and export. Record the sequence number from the PBM header.
3. Copy all six PBM files to a host test directory and run:

   ```sh
   python3 -B emulation/seamless/verify_probe.py --frame 33 --mode reference \
     /path/to/ZacProbe-0-reference.pbm /path/to/ZacProbe-1-reference.pbm
   python3 -B emulation/seamless/verify_probe.py --frame 33 --mode candidate \
     /path/to/ZacProbe-0-candidate.pbm /path/to/ZacProbe-1-candidate.pbm
   python3 -B emulation/seamless/verify_probe.py --frame 33 --mode screen \
     /path/to/ZacProbe-0-screen.pbm /path/to/ZacProbe-1-screen.pbm
   ```

   Replace `33` with the recorded sequence. Both references and rectangle-hook
   candidates should match exactly. With complete overlap, at least the
   obscured screen crop should fail: that is the negative control, not a
   seamless success.
4. Repeat after advancing the frame, changing the front window, separating
   the windows and resizing. Keep exports from successive trials separately.
5. Future general rendering-hook captures must use mode `candidate`, the same
   pixel/header contract and an independently recorded expected sequence.
   They must pass under complete overlap without cooperation from app code.

The verifier rejects malformed headers, oversized/truncated payloads,
duplicate windows, incorrect modes, stale sequences and any incorrect pixel.
It deliberately never claims that two matching test files prove seamless
compatibility, window discovery, blit handling or cross-process input.

## Validation and current gate

```sh
python3 -B tests/emulation/test_classic_probe.py -v
# Or through the existing Meson runner:
meson test -C /path/to/isolated-build classic-window-probe --print-errorlogs
```

Host tests include crop substitution, stale/relabelled frames, corruption,
resized/non-byte-aligned dimensions, malformed exports and agreement between
the original guest C pattern and the independent host oracle.

## Actual guest results

Ran the fixture with the installed SheepShaver against a disposable copy of the
user's existing ROM and guest hard disk, with only a dedicated test directory
shared. Sound was disabled and no network was configured. The original hard
disk was not attached. The exact guest OS version is not yet confirmed; these
results are not a claim of Mac OS 9.0.4 compatibility.

Exact-pixel mismatch counts from exported guest buffers:

| Trial | Frame | Window sizes A / B | Reference A / B | Candidate A / B | Screen A / B |
|---|---:|---|---|---|---|
| Initial control, B in front | 13 | 320x192 / 320x192 | 0 / 0 | Not installed | 61,440 / 0 |
| Initial control, A in front | 36 | 320x192 / 320x192 | 0 / 0 | Not installed | 0 / 61,440 |
| Rectangle hook, B in front | 25 | 320x192 / 320x192 | 0 / 0 | 0 / 0 | 61,440 / 0 |
| Rectangle hook, A in front | 46 | 320x192 / 320x192 | 0 / 0 | 0 / 0 | 0 / 61,440 |
| Resize A, A in front | 46 | 356x220 / 320x192 | 0 / 0 | 0 / 0 | 0 / 61,440 |
| Advance after resize, B in front | 66 | 356x220 / 320x192 | 0 / 0 | 0 / 0 | 63,257 / 0 |

The initial and hook trials used separate application runs, so their frame
sequences restart. The resized trials confirm that reference and candidate
buffers survive a real guest window resize; the frame-46 resize export is
intentionally paused. Cancelling Quit preserved both windows and allowed the
frame-46 export. After closing A and cancelling B's close, B's reference,
candidate and screen export all matched frame 66 exactly.

The disposable guest was shut down through Finder. The original hard disk's
SHA-256 still matched its pre-experiment value after the guest exited.

**Proven narrowly:** this guest's ordinary rectangle dispatch reaches our
per-port hook even when the destination window is completely obscured.
Replaying those calls into an independent port yields the exact changing
pattern, while framebuffer cropping fails. This is a useful pre-clipping
interception result, not general independent application rendering.

That first in-process trial did not verify external hook installation or
cross-process discovery. The subsequent experiments below address the bounded
rectangle case; color, arbitrary graphics state, text, scrolling/blits,
application-defined drawing procedures, input routing, latency and the
milestone-0 go/no-go gate remain unverified.

## Separate application/helper experiment

This experiment separates the fixture from the hook:

- **ClassicDrawingFixture** uses the same drawing, references and controls,
  compiled without `CLASSIC_PROBE_CAPTURE`. It installs no drawing hooks and
  produces only reference/screen exports. Its original application creator is
  `ZcDF`. The compiled executable was checked to contain none of the in-process
  capture symbols. Reference export is measurement support, not capture
  cooperation with the helper.
- **ClassicCaptureHelper** (`ZcSH`) initially tested Toolbox-only discovery.
  Its current native capture mode is documented in the next section.
  **Helper > Inspect windows** enumerates
  processes and its current Window Manager list into `ClassicHelperReport.txt`.
  Both enumerations are limited to 128 entries and explicitly fail at the limit.
- **Helper > Start observer** chains `PaintRect` and `EraseRect` Toolbox traps
  using Mixed Mode routine descriptors, counting calls and identifying `ZcDF`
  through the Process Manager. It does not change any window's `grafProcs`,
  clip/visibility regions or pixels, and does not synthesize candidate buffers.
- **Helper > Stop observer** restores saved trap entries before disposing the
  callbacks. Quit does the same. If another patch changes the chain, the helper
  refuses to unload its callback code; recover the disposable test guest rather
  than force-quitting it. Do not use this experiment in a normal guest session.

Reproduction in the disposable guest:

1. Put both resource-preserving applications in the dedicated test directory.
2. Launch the drawing fixture, let both windows advance, then pause.
3. Launch the helper and inspect: confirm both application processes exist,
   and record the fixture-window count visible to the helper.
4. Start the observer. Switch back to the fixture, resume, switch the front
   window, pause and export references/screens. Record the actual frame.
5. Reactivate the already-running helper and inspect again. Preserve the report
   and fixture PBMs together. Stop the observer, inspect to confirm
   `hooks_installed=0`, then quit the helper.
6. Quit the fixture through its confirmation dialogs and shut down the copied
   guest through Finder.

**Earlier runtime result: the Toolbox-only approaches did not achieve external
interception.** The helper enumerated both processes but found zero fixture
windows in its Window Manager list. After the hook-free fixture advanced from
frame 180 to 200 and changed the front window, the final observer report showed
37 intercepted calls, **zero fixture rectangles**, and no observation error.
Both fixture references were exact at frame 200; the obscured B screen crop had
61,440 wrong pixels. The observer worked for helper drawing, but did not observe
the separate fixture's drawing. An earlier rectangle-replay attempt similarly
produced no candidate buffers; unexercised replay code was removed.

These results establish that polling the helper's Window Manager list and
patching rectangle traps from an ordinary application are not sufficient in
this tested guest. They do not distinguish Process Manager trap scoping from
native PowerPC dispatch bypassing these patches, nor prove that all possible
system-wide hooks fail.

The observer was stopped and a second report confirmed restored trap entries
(`hooks_installed=0`). Both applications then exited normally, the disposable
guest shut down through Finder, and the original hard-disk SHA-256 remained
unchanged. All three guest targets build with strict warnings; the nine host
verifier tests still pass through Meson.

This led to the native execution-context experiment below. Do not proceed to
production host windows or claim the milestone-0 gate passed.

## Native PowerPC execution context and capture

Rather than adding an unproven startup extension, the next bounded experiment
tests native dispatch directly. **Helper > Native capture** resolves the
`PaintRect`, `EraseRect` and `DisposeWindow` exports from `InterfaceLib` with
the Code Fragment Manager. On Classic PowerPC, these function pointers address
eight-byte transition vectors containing entry point and TOC. The helper saves
those pairs, replaces them with its native callback pairs and calls the saved
vectors to preserve normal guest behavior. This is a disposable-guest
experiment, not a portable or production patch API.

The first native observer saw **75,873 fixture rectangle calls**, out of 75,950
total, with no observation error. It could traverse the Window Manager list
from the fixture's execution context even though polling from the helper
still found zero target windows. Thus the earlier Toolbox trap experiment
missed the native path; native dispatch offers a workable bounded interception
point. It does not establish exactly how the earlier traps were scoped.

The current helper additionally:

- Preallocates two maximum-size monochrome backing stores in the helper's own
  context. It never opens/disposes capture ports from the foreign process.
- Filters by the original fixture's `ZcDF` creator and exact window titles;
  validates bounded dimensions and refuses custom window drawing procedures.
- Initially replayed only known black/white rectangles. The current monochrome
  graphics-state/text/copy/scroll coverage is described below. Neither path uses
  the fixture's cooperative reference pixels or the host oracle to draw
  candidates.
- Uses the fixture's active GrafPort and process-context window list, without
  changing its `grafProcs` or clip/visibility regions. Capture-port region
  updates temporarily use the helper's heap zone and restore the caller's zone
  and current port before returning.
- Associates captures with window addresses and process serial numbers.
  Native `DisposeWindow` callbacks invalidate a closed window before chaining
  disposal; export additionally checks that each owner process still exists.
- Requires both live captures with matching 32-bit markers before exporting
  `ZacHelper-{0,1}-candidate.pbm`. Markers are decoded from intercepted pixels,
  not guessed from elapsed time. Windows narrower than 256 cannot encode all
  marker bits and are explicitly refused for this export experiment.
- Restores all installed native vectors before releasing capture storage or
  the CFM connection. Stop/Quit refuses to unload if another patch has changed
  any vector. Do not force-quit the helper while capture is installed.

### Reproduction and exact-pixel verification

1. Launch the hook-free **ClassicDrawingFixture** and pause.
2. Launch **ClassicCaptureHelper**, choose **Native capture**, and leave it
   running. Do not select the older Toolbox-only **Start observer** mode.
3. Reactivate the fixture, resume briefly, pause and export references/screens.
   Record the exported fixture frame independently.
4. Reactivate the helper and choose **Export candidates**. Preserve these
   exports with the fixture reference and screen files from this trial.
5. Verify, replacing `54` with the independently recorded fixture frame:

   ```sh
   python3 -B emulation/seamless/verify_probe.py --frame 54 --mode candidate \
     --references /path/to/ZacProbe-0-reference.pbm /path/to/ZacProbe-1-reference.pbm \
     /path/to/ZacHelper-0-candidate.pbm /path/to/ZacHelper-1-candidate.pbm
   ```

   Both pairs must match the independent oracle exactly at the expected frame.
   Candidate dimensions must match their corresponding fixture references.
   This prevents a stale, differently sized or self-labelled capture from
   being accepted as the current trial.
6. Repeat with the other window in front, after resize/advancement, and after
   closing/relaunching the fixture. Save successive trials separately.
7. Choose **Stop observer** (which stops either backend), inspect to confirm
   `native_installed=0`, then quit the helper normally.

### Recorded results

| Trial | Frame | Window sizes A / B | Reference A / B | Helper candidate A / B | Screen A / B |
|---|---:|---|---|---|---|
| Separate fixture, B in front | 54 | 320x192 / 320x192 | 0 / 0 | 0 / 0 | 61,440 / 0 |
| Advance, A in front | 69 | 320x192 / 320x192 | 0 / 0 | 0 / 0 | 0 / 61,440 |
| Resize A, paused | 69 | 356x220 / 320x192 | 0 / 0 | 0 / 0 | 0 / 61,440 |
| Advance, B in front | 84 | 356x220 / 320x192 | 0 / 0 | 0 / 0 | 63,257 / 0 |
| Fixture exits/relaunches | 13 | 320x192 / 320x192 | 0 / 0 | 0 / 0 | 61,440 / 0 |
| Final guarded helper build | 29 | 320x192 / 320x192 | 0 / 0 | 0 / 0 | 61,440 / 0 |

Values are meaningful-pixel mismatch counts. Relaunch resets the fixture
sequence and changes its process identity. After closing A, diagnostics showed
one disposed window, A's capture invalidated and B's capture retained. Pair
export was refused without overwriting prior files. After closing B and
exiting the fixture, both identities were invalidated and the disposal count
was two. Relaunch while the helper remained installed produced fresh captures.

The final build, including custom-procedure rejection, region error checks and
restoration readback, passed a fresh frame-29 capture trial. All six recorded
native trials pass the CLI's independent-reference comparison; each screen
pair fails its intended negative control. Ten host tests pass directly and
through Meson, and all three guest targets build with strict warnings.
Native vectors were restored, both applications exited normally and the copied
guest shut down through Finder. The original disk's SHA-256 still matches the
pre-experiment value.

**Proven narrowly:** separate-process native rectangle interception can produce
exact continuously changing hidden-window pixels without modifying the test
application. Native vector restoration and normal close/relaunch behavior have
been exercised. This does not pass the full feasibility gate.

The next experiment expanded monochrome state, text and scrolling/CopyBits as
recorded below. Color and broader lifecycle coverage (`CloseWindow`, crashes,
other patches and application-defined procedures) still need work. A startup-resident helper
or emulator bridge may still be needed for production callback lifetime,
transport and recovery; neither is implemented here.

## Monochrome drawing state, text, images and scrolling

**Probe > Graphics mode** in the separate hook-free drawing fixture adds an
advanced scene after the original frame marker/checkerboard:

- Explicit inset clipping, reversed foreground/background, a gray pen pattern,
  XOR drawing, non-default pen width/height, frames and a diagonal line.
- Two `DrawText` operations with different size, bold/underline/italic style,
  XOR/OR transfer mode and fractional extra spacing. Text uses the user's guest
  font installation at runtime; no font files or rendered glyph assets ship.
- An original small offscreen bitmap copied with scaling, XOR and a mask,
  followed by a self-copy from the window's existing pixels.
- `ScrollRect` with alternating horizontal direction, a vertical offset and
  the normal guest update-region output.

The helper adds native hooks for `DrawText`, `FrameRect`, `LineTo`, `CopyBits`
and `ScrollRect`. Before replaying it copies monochrome pen, foreground/
background, patterns, font/style/size/mode/spacing and the application's
**explicit clip region**. It deliberately does not copy visibility clipping.
Replay preserves the caller's port and heap zone; scratch-region allocation
stays in the helper's heap. Nested native calls are suppressed during replay
and while chaining the original compound operations, avoiding double capture.
The original operation still runs normally, preserving text/pen advancement
and the caller's scroll update-region result.

For a window self-copy, the helper reads its independent backing store, not
the obscured screen. Arbitrary screen-source aliases and PixMap/color sources
are explicitly refused rather than exported as successful captures. This
bounded path requires the destination to be the current fixture's exact
`portBits`; it is not a general QuickDraw blit implementation.

### Export contract and verification

Graphics scenes use a distinct version-2 PBM header:

```text
P4
# zacos9-classic-probe 2 candidate 0 36 graphics
320 192
```

The fixture labels graphics reference/screen exports automatically. Use
**Helper > Export graphics** (Command-G) for matching helper candidates.
Ordinary **Export candidates** remains version 1 for the original checkerboard
experiment. Preserve all exports from a single paused trial together.

Run the same independent-reference CLI command as above, with the actual
fixture frame. Version-2 captures cannot pass without references: the host
checks frame/header/profile/dimensions, verifies the unchanged 32-bit frame
marker, then compares **every meaningful pixel** against the separate guest
reference. Glyphs and compound drawing are reference-based, not predicted by
the checkerboard oracle. Padding bits are ignored. Graphics exports narrower
than 256 pixels are refused because they cannot encode the complete marker.
For the negative control, use `--mode screen --references ...`.

### Runtime results

All helper candidate pairs below matched their independent references exactly:

| Trial | Frame | Window sizes A / B | Candidate mismatches A / B | Screen mismatches A / B |
|---|---:|---|---|---|
| First graphics prototype, B in front | 55 | 320x192 / 320x192 | 0 / 0 | 59,998 / 0 |
| Final guarded graphics build, B in front | 36 | 320x192 / 320x192 | 0 / 0 | 60,136 / 0 |
| Advance, A in front | 52 | 320x192 / 320x192 | 0 / 0 | 0 / 60,136 |
| Resize A, paused | 52 | 356x220 / 320x192 | 0 / 0 | 0 / 56,307 |
| Advance, B in front | 67 | 356x220 / 320x192 | 0 / 0 | 58,321 / 0 |
| Return to original checkerboard mode | 82 | 356x220 / 320x192 | 0 / 0 | 63,257 / 0 |
| Final export-guard/cleanup build, B in front | 23 | 320x192 / 320x192 | 0 / 0 | 59,998 / 0 |

The final B-front report confirms all new hooks actually ran: 76 text, 38
frame, 38 line, 76 image/self-copy and 38 scroll replay calls, with no observation
error. Later trials exercise both scroll directions. This is measured pixel
coverage, not merely successful compilation or seeing a window.

**Color remains unsupported.** A **Color guard test** (Command-C) creates and
draws a temporary color window in the fixture. The real guest test confirmed
explicit `color port` rejection (Mac error -50), with no candidate exports or
overwriting previous evidence. Unrelated color alerts are ignored because
they are not fixture document windows. The helper never interprets a target
CGrafPort/PixMap as a monochrome GrafPort/BitMap.

Stopping/restarting capture after that rejection worked. Closing A invalidated
its capture while B remained live, and pair export was refused. All eight
native vectors were restored before helper exit; the copied guest shut down
through Finder and the original disk's checksum remained unchanged.
The final build's graphics trial also passed, then shrinking A below 256 pixels
confirmed explicit refusal by both fixture and helper without overwriting the
previous exports. Fixture graphics resources are released on normal exit and
initialization failures.
All three guest targets build with strict warnings. Fourteen host tests pass
directly and through Meson, including graphics profile enforcement, full-pixel
comparison, stale/relabelled markers, CLI reference requirements and original
version-1 behavior.

**Next prerequisite:** a dedicated color-port/GWorld backing-store experiment
with RGB/PixMap and color-state-preserving replay and a bounded color export
format. Do not enable color capture by casting through the monochrome structs.
General application clipping/origins, other text APIs, cross-window blits,
custom drawing procedures, lifecycle recovery and production integration also
remain unproven; milestone 0 has not passed.

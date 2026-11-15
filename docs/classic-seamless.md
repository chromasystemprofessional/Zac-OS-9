# Seamless Classic windows: implementation plan

Status: milestone 1 application transport implemented; milestone 2 guest
window discovery/capture and a basic host viewer are in progress. Target:
SheepShaver with user-supplied Mac OS 9.0.4 first. Basilisk II and other guest
versions are later work.

Milestone 0 is now started: an original PowerPC two-window probe and exact-pixel
host verifier are in [the feasibility spike](../emulation/seamless/README.md).
The probe builds and runs in a disposable copy of the user's Classic setup.
An in-process QuickDraw rectangle hook captures exact advancing pixels from
fully obscured windows, including after resize; screen cropping fails the
negative controls. The separate hook-free fixture/helper initially found that
helper-local window polling and Toolbox rectangle trap patches miss the other
application's drawing. A native PowerPC InterfaceLib transition-vector hook
now reaches the fixture's execution context and captures both hidden/visible
buffers exactly, with advancing frames, resize, close invalidation and app
relaunch tested. This is bounded black/white rectangle capture from one original
test application, not general compatibility or a production guest extension.
The latest monochrome graphics trial also matches styled text, explicit
clipping, patterned/XOR pen state, frames/lines, scaled bitmap copies,
window self-copies and scrolling under overlap and after resize. The separate
color-port/GWorld experiment now captures two overlapping direct-32-bit color
windows exactly: the fixture's references pass an independent host RGB,
marker and calibration/grid oracle, the helper's candidates match them every
pixel, and the obscured screen crop fails the negative control. Indexed color,
custom procedures and general color APIs remain unproven. The feasibility gate
has not passed: cross-process window discovery/identity, input, broader
lifetime handling and general graphics APIs/origins still need proof. The
exact tested guest OS version is not yet confirmed.

The host verifier's version-3 PPM path now has an independent RGB oracle for
the fixture's marker, calibration bars and 5-bit color grid, plus exact
reference comparison. It is no longer only a format contract; the direct
32-bit guest trial passes it. The 16-bit RGB555 conversion path is implemented
but has not been run on a 16-bit guest display.

## Goal and agreed scope

Double-click a Classic application in ZacOS Finder and see its document
windows alongside Linux application windows, with ZacOS window management and
menus. The application still runs inside emulated Mac OS; it is not a native
Linux program.

The target is **true independent windows with a full-desktop fallback**.
Screenshots or crops of the guest display are not the finished architecture:
they cannot reconstruct content obscured by another guest window.

Initial compatibility target: ordinary Window Manager/QuickDraw applications.
Custom window definitions, direct-screen drawing, games, full-screen tools,
and unsupported dialogs may require desktop mode. Do not promise universal
compatibility before testing.

Apple ROMs, system software, fonts, icons and reference assets are not shipped.
Guest application pixels are rendered from the user's own installation at
runtime. Develop our helper and host integration from original code using
appropriately licensed interfaces and tools.

## Existing foundations

- [Classic launcher](../shell/classic/zacos9-classic): starts SheepShaver with
  an 800x600 guest display, shared home directory and SDL/Xwayland output.
- [Finder](../shell/finder/finder.cpp): recognizes Classic application metadata
  and starts Classic with `--open`; document association is still to come.
- [Emulator build](../emulation/build-emulators.sh): pins a macemu revision
  and applies the Classic bridge patch from
  [`emulation/patches`](../emulation/patches/README.md). Do not edit generated
  build output or the excluded upstream checkout in place.
- [Compositor](../compositor/src/xdg.c): manages ordinary Wayland toplevels,
  server-side frames, activation, size requests and close requests.
- [Application integration](app-integration.md): global-menu support already
  exists for Linux applications. Classic needs explicit guest identities:
  a host bridge PID alone cannot distinguish several emulated applications.

These are integration points, not evidence that the guest rendering work is
already solved.

## Proposed architecture

```text
Finder / Classic controls
          |
          v
Classic session supervisor ---- full-desktop recovery window
          |
          +---- host window frontend ---- ordinary Wayland toplevels
          |              |
          |              +---- menu / clipboard adapters
          |
     versioned local bridge
          |
     patched SheepShaver
          |
     original guest helper
          |
     Mac OS 9 Window Manager / Process Manager / QuickDraw / applications
```

### Guest helper

Inventory applications and windows, assign session-scoped identities, and
report lifecycle, title, content bounds, window class, visibility, active
application, modal relationships and menus where supported.

Handle application/document open requests through guest OS mechanisms rather
than launching guessed commands. Activation, close and resize requests must
go through normal guest behavior: an unsaved-document prompt must not be
bypassed. Coordinate guest input and rendering at safe execution points.

The helper's form (application, extension, or a combination), supported APIs,
compiler and toolchain licenses are decisions for the feasibility phase.
Do not assume polling window lists alone can observe all guest processes.

### Emulator bridge and rendering

Transport metadata, frame updates and input between the guest and host.
Prefer a narrow emulated device or emulator-mediated channel over exposing a
network command service or using the entire shared home directory as IPC.
Select the actual transport only after verifying emulator hook points.

Independent pixel buffers require a validated rendering strategy. Candidates
to investigate are:

1. Guest-assisted redraw into offscreen ports with correct application and
   clipping context.
2. Emulator/guest hooks that maintain per-window backing stores and account for
   drawing, blits, clipping, moves and update regions.

Neither is assumed sufficient. QuickDraw calls, direct framebuffer writes and
custom window drawing may escape either approach. A stale cached crop is not
an acceptable substitute for independent rendering.

### Host frontend and supervision

Prefer a separate unprivileged frontend using ordinary Wayland surfaces,
initially software-backed buffers and server-side decorations. This avoids
making the compositor responsible for emulation or guest memory parsing.
Prototype the simplest frontend that can satisfy sizing, transient dialogs
and pixel accuracy; choose its toolkit during feasibility.

One guest session serves multiple Classic applications. A supervisor owns
startup/readiness, single-instance behavior, request queues, timeouts and
recovery. Closing one document does not terminate the emulator or another app.
The frontend must not inherit SheepShaver's low-memory capability.

## Milestones and acceptance gates

### 0. Feasibility spike: prove independent rendering

- Work from the pinned emulator revision in a permitted, separate source
  checkout; inventory licensed guest development options and bridge hooks.
- Create original guest test applications with two overlapping document
  windows, distinctive patterns, continuous updates and an unsaved-close dialog.
- Prove window discovery across processes, activation and safe input delivery.
- Compare both exported window buffers with their expected patterns while
  one guest window is fully obscured and continues changing.
- Exercise scrolling/blits, move, resize, update regions and window destruction.
- Verify arbitrary host placement does not require guest windows to fit
  simultaneously on the guest screen.

**Gate:** reproducible exact-pixel tests for both obscured and visible content,
and evidence of a workable guest event strategy. Record the proven rendering
mechanism and compatibility limits before building production integration.
If unsuccessful, stop the seamless implementation and document the blocker;
do not silently downgrade the requirement to cropping.

### 1. Session bridge and direct application launching

- Define a versioned protocol: handshake/capabilities, session generation,
  application/window identities, snapshots, events, acknowledgements and errors.
- Implement a single Classic session and bounded startup/readiness queues.
- Wire Finder's selected Classic application and documents to guest open
  requests, including existing sessions and resource-fork preservation.
- Define explicit mapping between host shared files and guest paths; files
  inside guest disk images need guest identities, not fabricated Linux paths.
- Keep desktop mode as the default while developing.

**Gate:** double-clicking two apps opens those apps in one session; a second
launch does not start another emulator or attach a writable disk twice.
Missing originals, guest readiness failures and rejected opens show an alert.

**Prototype status (2026-11-15):** the transport is now an emulator-side
bridge. The pinned SheepShaver carries `OP_CLASSIC_BRIDGE` (selector 53,
instruction `0x18000038`; patch in
[`emulation/patches`](../emulation/patches/README.md)). The emulator reads the
host request queue itself and writes acknowledgements, so the guest helper is
not affected by the guest File Manager's ExtFS cache and needs no serial port,
MacTCP or Open Transport. The helper is original Retro68 code in
[`emulation/classic-open`](../emulation/classic-open/README.md).

A clean Mac OS 9 guest opened SimpleText, then SimpleSound, in one Classic
session; both requests were acknowledged with `status=ok`. `zacos9-classic`
no longer refuses a second open: it serialises requests, waits for each
acknowledgement, and reports a guest error or timeout. The milestone 1
application gate is met for applications that are files below the shared home
folder. Documents, applications inside guest disk images, and guest window
discovery/input remain unimplemented; the request queue is versioned, but a
full handshake/capability exchange and guest window identities are still to
come.

Earlier prototypes used the shared-folder queue directly, the SheepShaver
serial port, MacTCP, Open Transport and Finder Apple Events. The shared-folder
view was cached by the guest; the serial driver needed a library the test
install lacked; MacTCP stopped receiving after an application switch; Open
Transport client libraries were missing; and the Finder rejected app items on
the shared volume with error -903. Do not reintroduce those paths without new
evidence. The helper's SIZE resource also needed `canBackground` so Mac OS
does not suspend it when another application comes forward; `patch_size.py`
sets it.

### 2. Opt-in independent document windows

- Export each supported document window as a host toplevel, using the rendering
  strategy proven in milestone 0.
- Export guest content only; avoid duplicate guest and host title bars.
- Keep stable identities across title changes; handle create/hide/show/destroy.
- Define coordinate transforms between host content and guest windows.
- Forward host focus, mouse and keyboard to the appropriate guest process;
  guest clicks may activate first without dropping the intended click.
- Handle close requests, fixed-size windows and acknowledged resize results.
  Host window movement need not move the guest window on its emulated screen.
- Retain normal compositor stacking and placement instead of importing guest
  coordinates as mandatory host geometry.

**Gate:** two Classic apps and a Linux app interleave independently. Resizing
does not stretch stale pixels, moving one window does not move another, and
unsaved close can be cancelled. Repeated focus switches leave no stuck input.

**Prototype status (2026-11-15):** the guest half is working. An original
background agent ([`emulation/classic-windows`](../emulation/classic-windows/README.md))
installs native InterfaceLib transition-vector hooks and identifies windows
from separate processes. In a Mac OS 9.0.4 guest it discovered and captured
SimpleText and Graphing Calculator 1.1 windows, including typed text, while
writing PPM frames to the shared volume. A basic host frontend
(`zacos9-classic-windows`) shows those frames as ordinary Qt toplevels with
guest window titles and a controller that can hide them for desktop mode.
Input forwarding, guest close/resize requests, document routing and a fully
persistent **Show Classic Desktop** action are not implemented, so the gate
above is not met. The current frame transport (rewriting PPM every two seconds)
and bounded operation coverage are prototype work, not production
compatibility.

### 3. Modal dialogs, popups and full-desktop recovery

- Represent supported dialogs as transient host windows with guest-correct
  application/system modality; palettes and popups need distinct roles.
- Treat guest menus, drag tracking and synchronous event loops explicitly:
  these can block normal metadata polling.
- Unsupported window classes or direct-screen modes trigger a visible
  desktop-mode offer, not invisible controls or discarded input.
- Provide a persistent **Show Classic Desktop** action.
- Switching modes must preserve running apps and unsaved documents, clear
  pressed input state, and use one coherent input destination.

**Gate:** Save/Open dialogs, alerts and menu tracking remain usable.
The user can always return to the full guest desktop without restarting it.
Recovery cannot depend on the guest helper being responsive.

### 4. Global menus and application identity

- Associate each exported window with its guest application, not just the
  shared frontend PID. Use a namespaced, session-scoped identity.
- Integrate with the current menu adapters where possible; add a small
  authenticated per-window identity mechanism only if existing plumbing
  cannot distinguish guest apps.
- Export enabled/check states, submenus and dynamic updates. Menu commands
  return to the correct guest app with validation against the current menu.
- Keep ZacOS system controls distinct from guest application menus.
- Unsupported/custom menus remain usable through desktop mode.

**Gate:** focusing app A/B/Linux switches to the correct menus. Disabled
commands cannot execute, stale events are rejected, and Quit affects only the
intended guest application.

### 5. Clipboard, documents and Finder experience

- Add opt-in plain-text clipboard transfer with Mac encoding conversion and
  loop prevention; specify unsupported text replacement behavior.
- Never automatically transfer clipboard contents to an unfocused guest.
- Validate that the selected clipboard mechanism works for the frontend under
  ZacOS Wayland; do not assume background clipboard access.
- Add guest application identities/icons and document associations without
  placing system utilities back in Applications.
- Preserve file type/creator and resource forks for supported file exchange.
- Defer rich clipboard formats and cross-desktop drag-and-drop until text and
  open/save behavior are reliable.

**Gate:** text copy/paste round-trips supported characters; open/save preserves
resource forks and does not modify unrelated host files.

### 6. Compatibility, packaging and release

- Maintain a tested matrix covering ordinary QuickDraw apps, multiple windows,
  fixed-size dialogs, custom controls, palettes and unsupported full-screen
  behavior. Use original fixtures plus user-supplied apps for manual checks.
- Package original guest helper, host frontend and reproducible emulator
  patches separately from all user-supplied Apple software.
- Make helper installation explicit and reversible, with a guest disk backup
  recommendation. Do not modify disk images while the emulator has them open.
- Feature-flag seamless mode; initially default off, enable per tested session,
  and retain desktop mode permanently.
- Test updates with old/missing helpers: negotiate compatibility, report why
  seamless mode is unavailable, and leave desktop mode working.

**Gate:** focused tests, production builds and interactive dev-boot checks
pass before a Debian release. No ISO rebuild is required for iteration.

## Reliability and trust boundaries

- Treat all guest/emulator messages as untrusted: validate lengths, rectangles,
  pixel formats/stride, window counts, IDs, sequences and payload sizes.
- Proposed initial limits: 128 exported windows, 4096x4096 per window,
  256 MiB total buffers, bounded event queues and a 3-second health timeout.
  Negotiate lower platform limits and reject excess explicitly; confirm these
  limits against hardware measurements before release.
- Use private per-user runtime endpoints and peer/session checks. Never
  accept guest-provided shell commands or arbitrary host executable paths.
- Keep clipboard and additional file access opt-in. The existing shared-home
  behavior must be documented, not described as a sandbox.
- On helper timeout, suspend seamless controls and offer desktop recovery.
  On emulator exit, remove stale windows and report the failure without
  automatically restarting writable disks.
- Disconnect or fallback during a drag must release buttons/modifiers.
  All external process work in Qt remains asynchronous; no `waitForFinished()`.

## Verification strategy

1. Protocol unit tests: malformed frames, limits, stale sessions, reused IDs,
   missing acknowledgements, disconnect and state resynchronization.
2. Guest fixture tests: exact pixel patterns behind overlapping windows,
   continuous updates, clipping, blits, event ordering and modal responses.
3. Host frontend tests with a fake bridge: independent surfaces, transient
   relationships, focus, resize acknowledgements and compositor close requests.
4. End-to-end tests: launch from Finder, two guest apps plus Linux, unsaved
   close, menus, clipboard, scaling and desktop-mode transition.
5. Failure tests: absent/incompatible helper, guest crash, stalled event loop,
   frontend restart, oversized messages and emulator termination.
6. Performance measurements: proposed target on a documented reference machine
   is p95 input-to-present latency below 100 ms and 30 fps for continuously
   updating supported windows, with no unbounded idle polling. Measure rather
   than assume; reduce scope explicitly if the emulator cannot meet it.

## Delivery order and open decisions

Do milestone 0 first. Once its gate passes, milestone 1 provides useful direct
launching even before seamless mode is ready. Milestones 2 and 3 form the first
usable opt-in seamless release. Menus and clipboard follow, with desktop
fallback available throughout.

Still to decide from evidence: guest helper/toolchain, rendering interception
strategy, bridge transport, frontend toolkit, guest screen/workspace model,
menu identity plumbing and the initial application compatibility list.
No schedule estimate is reliable until the rendering spike passes.

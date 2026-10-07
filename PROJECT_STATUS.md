# Project Status

Updated: 2026-10-07

## Completed

| Commit | Feature |
|---|---|
| `cd4f6ed` | Silent start-up: white screen, platinum sparkle logo, Welcome screen; `scripts/dev-boot.sh` |
| `d1f5765` | File Sharing control panel (AFP + SMB toggle, shared folders list) |
| `02cd5a4` | Get Info > Sharing view, pixel-matched to Mac OS 9's original |
| `2349968` | `network/afp/zacos9-afp`: AFP 2.x FUSE client skeleton |
| `3dbae73` | AFP client fully working: single-`sendmsg` fix, busy-delete retry, truncate fallback; `tests/sharing/afp-client.sh` passes on Mac OS 9 and Netatalk |
| *(uncommitted)* | Virtual Macintosh filesystem for the Finder, with real application icons, package origin, and hide-not-uninstall — see below |
| *(uncommitted)* | Fetch Software (app catalog over apt) and New Tricks (software updates) in the Apple menu — see below |
| *(uncommitted)* | Network Browser: connect to AFP and SMB servers, mounted volumes on the desktop — see below |
| `4e3ebe6` | ZacOS 9 rename (from Platinum 2026) |
| `996b311` | Applications: empty on fresh install, flat launchers |
| `634add8` | Finder: local disk volumes on the desktop (GVolumeMonitor) |
| `8771336` | Windows app compatibility: `.exe`/`.msi` open via Wine |
| *(uncommitted)* | Mac OS 9-style installer: Welcome → Select Disk → Installing → Done, runs `zacos9-install` via polkit; partition-level install (preserve FAT/HFS+ sibling partitions) |
| *(uncommitted)* | New ZacOS 9 SVG logo at 16×16 (menu bar) and 64×64 HQ (startup, installer) with real alpha blending |
| *(uncommitted)* | Installer fixed end to end: GRUB packages in the image, UEFI/BIOS detection, BIOS boot partition, fallback `EFI/BOOT/BOOTX64.EFI` for Macs, live user in `sudo` (polkit), failing step shown in the window. VM-tested (`build/test-install.py`): whole disk under BIOS and UEFI, and a partition beside a kept FAT partition under UEFI — each installed disk boots to the desktop. Boot splash now shows the logo and a progress bar. |

## Current work

**Classic monochrome graphics coverage** (unreleased, gate not passed):
expanded native capture to preserve monochrome pen/text/color-value state and
explicit application clipping, with `DrawText`, frames/lines, masked/scaled
`CopyBits`, backing-store self-copy and `ScrollRect` hooks. Replay and original
compound-call guards prevent nested double capture; helper scratch regions stay
in the helper heap, and the caller's port/zone are restored. The hook-free
fixture's opt-in graphics mode exercises styles, XOR/OR modes, spacing, pen
patterns, clipping, images and alternating scroll directions. Version-2 exports
require independent guest references and full meaningful-pixel comparison.
Actual graphics trials match both hidden/visible buffers at frames 36, 52 and
67, including 356x220 resize; a frame-82 original-pattern regression also passes.
Diagnostics confirm every added hook ran. Color-window testing explicitly
refuses capture without overwriting old evidence; color/PixMap/GWorld capture
is the next prerequisite, not implemented. Capture restarts after rejection,
closed-window invalidation and all-eight-vector restoration tested. Fourteen
host tests and strict builds of all three guest apps pass. Copied guest shut
down through Finder; original disk checksum is unchanged.
See [graphics experiment](emulation/seamless/README.md#monochrome-drawing-state-text-images-and-scrolling).

**Classic native execution-context capture** (unreleased, gate not passed):
the helper now resolves PowerPC InterfaceLib transition vectors through CFM
and reversibly hooks `PaintRect`, `EraseRect` and `DisposeWindow`, chaining
saved native entry/TOC pairs. This reaches the separate, hook-free fixture's
process context without modifying its binary, window procedures or clipping
regions. Native observation counted 75,873 fixture calls; its process-context
Window Manager list is accessible there. Helper-owned independent buffers
match both references exactly at frames 54 and 69 with either window fully
hidden, after resizing A to 356x220, and after advancing to frame 84 with B in
front. Screen crops still fail the hidden-window controls. Window close
invalidates captures; exports refuse a closed pair. App exit/relaunch produces
fresh exact frame-13 captures. Callback lifetime remains tied to the running
helper; it refuses unsafe unloading if another patch changes the vector chain.
No startup extension or emulator patch was necessary for this bounded result.
Ten host verifier tests pass, including independent frame/dimension/reference
checks. General text/color/blit capture and production recovery remain unproven.
The final guarded helper passes a fresh frame-29 trial. All six native trials
pass independent-reference CLI checks; screen negative controls fail as expected.
Native vectors restored, guest apps exited normally and copied guest shut down
through Finder; original disk checksum remains unchanged.
See [native capture experiment](emulation/seamless/README.md#native-powerpc-execution-context-and-capture).

**Classic separate-app/helper trial** (earlier result, Toolbox-only path blocked):
split the original drawing fixture into a separate `ZcDF` application with no
capture hooks and added a `ZcSH` diagnostic helper. Shared surface/export code
preserves the first in-process probe. Strict PowerPC builds pass for all three
apps. In the copied guest, Process Manager enumeration sees both apps, but the
helper's Window Manager list contains zero fixture windows. A reversible
rectangle-trap observer sees helper calls but zero fixture rectangles while
the separate fixture advances from frame 180 to 200 and changes front window.
Frame-200 references remain exact; the hidden screen crop fails all 61,440
pixels. Removed unexercised external replay code rather than claiming capture
success. The subsequent native CFM-vector experiment above establishes target
execution context without a startup extension. Precise Toolbox trap scoping
remains unresolved; no production integration.
Verified observer teardown, normal app exits and Finder shutdown of the copied
guest; original disk checksum is unchanged. Nine host verifier tests pass.
See [separate-app experiment](emulation/seamless/README.md#separate-applicationhelper-experiment).

**Seamless Classic rendering spike** (unreleased, gate not passed): inspected
the pinned macemu revision in a separate temporary checkout. SDL presents one
guest framebuffer; Native QuickDraw acceleration hooks and native thunks are
candidate integration points, not independent per-window backing stores.
Added an original PowerPC two-window QuickDraw probe with overlapping animated
patterns, cooperative offscreen reference buffers, rectangle-hook candidate
buffers, screen-crop exports,
resize and cancelled-close controls. Cross-compilation with the open-source
Retro68/Multiversal toolchain passes strict warnings and generates resources,
MacBinary and an application-only HFS disk. Added an exact-pixel host verifier
and nine focused tests for stale/corrupt/cropped frames and limits, wired into
Meson as `classic-window-probe`. Actual trials in a disposable copy of the
user's Classic setup passed exact-pixel reference and rectangle-hook capture
checks with either window fully obscured, after advancing the frame and after
resizing to 356x220. Hidden screen crops fail completely (61,440 wrong pixels
at 320x192); partial-overlap cropping also fails. Cancelling Quit preserves
both windows, and closing one preserves the other's exact pixels. The copied
guest was shut down through Finder; the original disk's SHA-256 is unchanged.
This proves only controlled black/white rectangle interception
before visibility clipping, not general rendering. No production emulator or
launcher changes, original guest disk writes or Apple SDK redistribution.
Unmodified app hook installation, cross-process discovery, other drawing
operations and the feasibility gate remain unverified. Exact guest OS version
is not yet confirmed. See [spike instructions](emulation/seamless/README.md).

**Seamless Classic windows plan** (milestone 0 started): agreed target is independent
SheepShaver application windows with a permanent full-desktop fallback, not
screen cropping. [Implementation plan](docs/classic-seamless.md) starts with
a rendering feasibility gate for obscured windows, then covers a guest helper,
versioned bridge, direct launching, host windows, dialogs/recovery, menus,
clipboard and compatibility packaging. No seamless implementation or schedule
is claimed; general guest rendering still needs proof.

**Alias icon inheritance** (0.1.30): Finder resolves symbolic links before
choosing their icon, so aliases retain the original's file-type icon and
application aliases (including chains) use its custom 32- and 16-pixel icons.
Shared-folder aliases inherit the shared-folder icon. Alias names, paths and
italic labels remain independent; broken or looping links use a generic icon.
Focused desktop and selection tests pass, including custom application icons,
file-type icons, alias chains, shared folders and unavailable targets; the
production Finder target builds successfully.

**System Folder resource providers and read-only system views** (0.1.28):
Finder's virtual registry now includes resource-provider nodes under System
Folder (Sounds, Startup Items, Application Support, Device Drivers, Network,
System Logs, Software Components and Extensions) plus trusted launchers for
System Information and Extensions Manager. Providers are allowlisted by id and
lazy-loaded; no provider accepts arbitrary registry paths or commands. Alias
providers canonical-check source paths against allowed roots and suppress
broken, looping and out-of-root links. Resource entries remain virtual: they
cannot be renamed, labeled, dropped into, trashed or treated as uninstallable
applications. Protected logs stay informational only. Get Info now supports
virtual resource details (kind, location, source and access) for curated items
without a single filesystem counterpart. System Information reports installed
ZacOS version, distro, kernel, CPU, memory, storage, network, audio, loaded
drivers and uptime. Extensions Manager is intentionally read-only in this
phase, showing status/startup metadata without load/unload toggles.
Focused Finder VFS, desktop and selection suites pass in an isolated build;
one transient `finder-selection` timing failure was resolved by immediate
re-run in the same build directory.

**User applications separated from system utilities** (0.1.27): Finder
now excludes automatically installed Debian dependencies as well as the
installation-image baseline from Applications. Settings, system tools,
file managers and shipped utilities are shown in System Folder > Utilities,
including qps, PCManFM, desktop/screen-saver settings, Print Settings and
balenaEtcher, even when explicitly installed. Ordinary user utilities,
manually installed Debian applications, user-local launchers, Wine apps and
Flatpak exports remain eligible for Applications. Apt mark changes refresh
the view; desktop-file ownership supplies package identity, with cached
queries invalidated when dpkg's database changes. Registry version 9 moves
the shipped Utilities folder and system-launcher rename/label overrides
without removing user nodes. System tools and automatic dependencies cannot
be queued or removed through Finder's application Trash path, including an
already queued app subsequently reclassified as a system tool. No packages
are removed. The production Finder builds, and focused Finder routing,
desktop and selection suites pass;
installed systems receive the change through Software Update. Apt marks on
this machine confirm all four named utility packages are automatic, but
installation metadata cannot reconstruct user intent if another installer
marked a package manual.

**Trash queues application uninstall** (0.1.26): moving a supported
Debian or user Flatpak application to Trash via the menu or a drag creates
a persisted uninstall record and hides its launcher, without uninstalling
yet. Put Away or dragging back to Applications cancels removal. Empty Trash
confirms app names/sources and shows progress, then uses the existing
software helper or user Flatpak uninstall, retaining personal data. Failures
keep pending entries for retry/restoration and do not delete ordinary Trash
contents. Unsupported or system-wide Flatpak apps report an explicit error.
Application aliases remain ordinary links and never uninstall their targets.
Finder builds and focused Finder/Store tests pass with mocked uninstall
commands; tests cover separate-process queue persistence, confirmation
cancellation, failed and no-op uninstalls, personal-data preservation and
ordinary Trash retention on errors. No real application was uninstalled.

**First-install Flatpak launcher discovery** (0.1.26): Finder now watches
the existing ancestors of missing application directories and reacts to GIO
application-list changes. A first Flatpak installation (such as Chaski) can
create its export directory after login and appear in Applications without a
manual refresh or reboot. Reloading also picks up newly created icon-theme
directories. Tests cover symlinked exports, automatic folder refresh,
Flatpak identity, icons, removal and export-directory replacement.
The automatic discovery regression and Store browser suite pass; a normal
refresh was requested in the live Finder for the already installed Chaski.

**Persistent local-administrator Mac mount authorization** (0.1.25):
the existing polkit mount rule now includes the constrained read-only Mac
mount/eject helper. Active local members of `sudo` no longer receive an
administrator-password prompt each time a permanently connected Mac drive
is mounted after reboot. No password is stored; non-admin, remote and
inactive sessions still use the policy's authentication defaults. Disk erase,
encrypted unlock and other-user mount actions are not granted by this rule.
The JavaScript authorization regression passes all 64 action/session/group
combinations; all three focused authorization and disk-helper suites pass.
The rule is installed through the existing package path; reboot behavior on
the connected physical drive still needs confirmation after installing it.

**Real extension parade at startup, with Welcome on the boot splash**
(0.1.32, 2026-10-07): the icons along the bottom of the startup screen
used to be six fixed pictures shown on a timer after logging in. Now they
are the curated extensions (`lib/extensions.c`, shared with Extensions
Manager) appearing as their kernel modules actually load.
- The boot splash shows "Welcome to ZacOS 9" half a second in, drawn from
  the compositor's own code at build time (`boot/make-splash.c`). It
  matches pixel for pixel; bar fills 4–218 were checked against
  `lib/welcome.c`.
- `zacos9-parade` sends each icon to the splash as its module loads.
- The splash hands over to the compositor without the white gap
  (`zacos9-boot-handoff`: deactivate, then `quit --retain-splash` once the
  wm has the display). There are fallbacks so the text login is never hidden.
- See `docs/boot.md`. Tests added: `boot-parade`, `boot-handoff`,
  `boot-splash-welcome`, and a handoff phase in `startup-displays`.
- Still to confirm on real hardware after a reboot: that the handover
  shows no white flash on i915. greetd may switch the console to text
  mode as it starts, and only a real boot shows that.

**Extensions Manager: friendly names, icons and a curated list**
(0.1.32, 2026-10-07): instead of 145 raw kernel module names, Extensions
Manager and System Folder > Extensions now show about 45 curated entries
(only the ones loaded on this computer), for example "HDMI Sound", "Bluetooth",
"Keyboard & Mouse", "Linux Disks (ext4)". Each entry has a new Platinum icon
(11 new icons: puzzle piece, display, Ethernet, Wi-Fi, USB, keyboard, camera,
power, sensor, padlock, chip), a description and the modules it covers.
Helper libraries are hidden; **Show All** lists every module with an icon.
The window stays read-only. `extension-catalog` test added; full suite passes.

**Software no longer stuck loading Flathub** (0.1.31, 2026-10-07): opening
Software sat on the Featured page with the busy sweep running on every
machine. The Flathub AppStream download takes about 80 seconds here, but the
catalog helper killed it at 60, so it never finished and every visit started
over. The refresh now runs detached and always completes; a cached catalog
is read at once (about 9 s for 3,376 apps) and refreshed in the background;
a first download that is still running reports "still downloading". Featured,
which only shows curated Debian apps, no longer loads Flathub at all.
Catalog (31) and store-browser tests pass.

**Flathub metadata and combined category browsing** (0.1.25): Software
reads the enabled user remote's AppStream metadata for application names,
summaries, cached icons and categories, instead of relying on the sparse
remote listing. Flathub applications join the main categories and All
Applications through a remembered "Include Flathub applications" checkbox,
checked by default; hiding them does not disable the remote or remove apps.
The dedicated Flathub view remains available. Refresh replaces categorized
results without duplicates and preserves the prior catalog on errors.
Runtime dependencies now explicitly include Flatpak and AppStream bindings.
Software builds and all three focused Store/catalog suites pass, including
26 Python catalog tests and offscreen checkbox persistence, automatic loading,
category/search filtering, pre-install icons and refresh/error regressions.
The real official remote yields 3,371 applications across all six main
categories, with 3,356 summaries and 3,296 cached icons; missing published
metadata uses the existing fallbacks.

**Flathub remote validation correction** (0.1.24): Flatpak 1.16 omits
the trailing options column when empty. The Store incorrectly treated the
official two-column remote row as an unofficial source, and the catalog
helper rejected it as malformed. Both parsers now accept an omitted empty
options column while retaining exact official URL checks and disabled-remote
handling. Regression tests cover both output forms and reject malformed
rows and unofficial URLs.
Real Flathub catalog rows also omit empty descriptions; those now load with
an empty blurb rather than aborting the entire catalog.
Software builds and all three focused Store/catalog suites pass. The actual
configured official user remote loads 3,371 application entries successfully.

**Relevant APFS volumes and real disk names** (0.1.23): enumerate stored
APFS names through libfsapfs, exclude standard helper names (Preboot, Recovery,
VM, Update, xART, iSCPreboot, Hardware and Diagnostics), and mount remaining
data/system volumes with their original numeric indexes. Finder shows each
normal volume by its stored name, such as Chromasystem SSD, and opens its
contents directly; eject still unmounts the complete container. Filtering is
exact-name based because the Debian reader lacks a public role API; renamed
helper volumes cannot be identified automatically. Finder builds; 30 Mac-disk
tests pass, including filtering, name enumeration and rollback regressions.
All four focused Meson suites pass; the hotplug suite also checks the exact
desktop names, browse paths and eject identity for a system/data volume pair.

**APFS busy-directory correction** (0.1.22): the reported volume 4 is
present in the kernel mount table but `os.path.ismount()` returns false.
The 0.1.21 helper treated it as unmounted and tried removing its directory,
masking the mount failure with EBUSY. Mount detection and image-cache checks
now use `/proc/self/mountinfo`. Rollback checks every attempted volume,
including the failed reader's mount, and preserves the original error if
cleanup fails. Eject discovers orphan child mounts even without a record;
leftover mounts receive recovery records so Sniffer can offer eject.
Verified the detection fix against the actual stranded volume 4 without
modifying it. All four focused suites pass, including 27 Mac-disk tests;
mounting and browsing the affected volume still need verification after install.

**APFS boot mount correction** (0.1.21): the 0.1.20 helper passed `-f all`
to fsapfsmount, whose numeric-only parser rejects it despite advertising the
option. Multi-volume APFS containers then failed with an invalid volume index.
The helper now enumerates containers through libfsapfs and mounts each volume
by its one-based number. Desktop discovery recognizes the child mounts, eject
unmounts all children, image cleanup tracks those actual mountpoints, and failed
mounts roll back prior volumes. Regression tests cover numeric selection,
multi-volume mount/eject and rollback, including the installed reader's parser.
Finder builds and all four focused suites pass (22 Mac-disk tests). Actual
APFS mounting on the reported drive still requires verification after installation.

**Read-only Mac disk mounting** (0.1.20): physical HFS/HFS+/APFS volumes
and Mac disk images mount through a constrained block-device helper, with
polkit authentication, read-only source descriptors and ro/nodev/nosuid/noexec
mounts. Sniffer image double-click mounts instead of launching Classic;
the Classic menu still opens selected raw images. Raw APM/GPT, Disk Copy 4.2,
UDIF DMG, sparseimage and sparsebundle containers are supported. Desktop
icons include APFS FUSE mounts; eject removes image loops and converted
cache files after their final partition is unmounted. Errors remain visible,
and APFS is excluded from the erase/initialization helper. See
[Mac disks](docs/mac-disks.md) for backend limitations. Privileged mounting
on real hardware remains to be verified; no physical disks were modified
during automated tests. Finder builds; six focused Meson suites pass, including
17 Mac-mount helper tests. A synthetic sparse image was also read with Debian's
actual libmodi backend, verifying the converted data and untouched source.

**Classic sound set import** (uncommitted): `wmov` drag and release sounds
now come from the `snd#` slots classic sets actually use (slot 1 and slot 3),
and are not rejected when they share IDs with other drag codes. That is why
window dragging was silent. μ-law, IMA4, `twos` and uncompressed CmpSoundHeader
samples decode; leading null commands and low (1 kHz+) rates are accepted;
empty resources are silent instead of errors. Limits are raised to 256 sets,
30 s and 8 MiB per sample. All 75 local sets import with no errors (49 with a
drag sound, 18 with a release sound). Only MACE is still unsupported.

**GTK 3 global menus** (0.1.15): Added GTK's version-one Wayland menu
metadata protocol and per-focused-surface menu paths over platinum-shell v4,
preserving earlier clients and distinguishing windows of the same process.
The menu bar imports GTK GMenuModel/GActionGroup exports, live enabled and
checked/radio states, sections/submenus and action targets; asynchronous owner
PID verification rejects spoofed/stale exports and owner loss restores fallback.
Native GTK exports work directly. A realization-safe ZacOS GTK module reuses
Debian's appmenu parser for legacy GtkWindow menu bars: the stock module's
25.04 legacy Wayland export fails before realization and sends the wrong
action path. Session/dependency wiring installs and enables the corrected
module; legacy local menus reappear when the menu service disappears.
Isolated headless tests verify real Galculator File/Edit/Help global titles,
legacy/native GTK action clicks, same-process window focus/close, local-menu
fallback and real Qt registration/action compatibility. Backend tests cover
updates, toggles/radios, invalid paths, owner loss and PID mismatch. This is
released together in 0.1.15; the compositor/session need a coordinated
update and logout/login. GTK forced to X11 and custom/non-exported menus remain
outside this implementation.
The compositor, menu bar, Finder and GTK module build successfully. All
eight focused GTK, Qt-action, launch-feedback, Finder and native-snapshot
regression suites pass with no skips or failures.

**Sniffer copy/move status dialogs** (0.1.15): File transfers and
Duplicate now show a Platinum movable-modal dialog with current filename,
destination, progress bar, percentage and Stop. A worker thread counts and
copies folder contents in bounded chunks while the GUI remains responsive.
Stop/Escape/Command-period keep completed items, remove the current incomplete
copy and preserve originals; same-disk rename moves stop between items.
Aliases (including relative and dangling links) and resource-fork companions
are preserved. Explicit errors cover conflicts, read/write failures and
failed cleanup. Finder-originated drops release the drag grab before showing
the modal controls, and stopped/failed external drops are not reported as
completed moves. Tests cover copy interruption during actual file I/O,
completed-item retention, move status, keyboard Stop, recursive alias copies,
read-error rollback including read-only copied directories, and existing
Desktop/resource-fork behavior. Movable-modal windows now occupy a compositor
layer above ordinary app windows, keeping their Platinum frames visible.
Content-inset correction (0.1.17): xdg scene trees already compensate for
window-geometry offsets; the compositor no longer subtracts them a second time.
App content now starts below the classic title bar at the existing frame margins,
including movable-modal dialogs and transitions out of fullscreen.
The compositor builds; content-inset and fullscreen-protocol regression tests
pass. Live verification in the installed session is still pending.
Default window size: new main windows from other programs (xdg-shell and
XWayland) open nearly full-screen, inset 96 px from each side and 32 px from
the menu bar and screen bottom so desktop icons stay visible. Finder windows,
dialogs, fixed-size windows and fullscreen requests are unchanged. The
`default-window-size` test covers this, and the full Meson suite passes;
live-session verification is pending.
Installed-session drag/dialog interaction
still needs interactive verification after updating the Finder. Sniffer
builds successfully; all three focused selection, Desktop and VFS suites
pass, including pixel checks of the Platinum dialog and progress fill.

**Classic startup on installed hardware**: The user's Power Mac 7300–9600
Old World ROM, Mac OS 9.0 CD image and 2 GB hard-disk image were recognized,
but installed SheepShaver exited before opening a window with
`Cannot map Low Memory Globals: Operation not permitted`. With explicit
administrator authorization, `cap_sys_rawio=ep` was applied only to the
root-owned installed SheepShaver binary. Global `vm.mmap_min_addr` remains
65536. Relaunch created an 800×600 emulator window, and the user confirmed
the Mac OS desktop or installer appears. This is a local capability change,
not an automatic package permission grant; replacement of the binary during
an emulator upgrade may remove it. The permission's risks and removal are
documented in `emulation/README.md`.
Packaged (next release): `zacos9`'s postinst grants the same capability to
`/usr/libexec/zacos9/SheepShaver` on install and upgrade (Software Update
carries it). A dpkg file trigger reapplies it when `zacos9-emulators`
replaces the binary, and the emulator package's own postinst grants it too.
Both packages depend on `libcap2-bin`. A setcap failure only warns.

**Real Desktop and save locations** (0.1.15): Finder continues to show
the real XDG Desktop directory while retaining startup/mounted disks and
Trash. Login registers a missing XDG Desktop mapping and GTK bookmark without
replacing existing paths, files or bookmarks; the Qt style appends Desktop to
save-dialog places and portal file choosers prefer GTK. Virtual application
and backed-folder drags make real Desktop aliases without moving originals;
Command-Option-drag makes aliases of ordinary files/folders. Alias copies
preserve links, unavailable originals give an alert, and transfers resolve
destination aliases before checking for recursive folder copies. Offscreen
tests cover external saves/watcher updates, New Folder, actual application
and backed-folder drag payloads, alias opening/copying/trashing, and saving
through a Desktop folder alias in a real Qt file chooser. Registration tests
cover custom paths, existing bookmarks, repeated login, invalid paths and
symlinked configuration files. Finder and the Qt style build; all five focused
Desktop, GTK, selection and VFS suites pass. A real GTK chooser also verifies
the Desktop sidebar and saving through its folder alias. Installed-session
portal routing still requires verification after updating the package and
logging in again.

**Native Screen Snapshot replacement** (0.1.15): Following a repeatable
report of YouTube playback failing after `grim` captures, Snapshot now uses a
native private Wayland interface to read the last committed display buffer.
Unlike screencopy, it does not change output-render or software-cursor locks.
The compositor retains one current displayed buffer per output, releases it
on replacement/reconfiguration/removal, and copies requested images into sealed
memory files. The native client validates dimensions, imposes a 32-megapixel
limit, combines monitor images at the selected scale, crops and atomically
saves PNGs. Rectangle selection, Desktop naming and clipboard behavior remain.
There is no automatic grim or unverified GNOME fallback. Old compositors give a
restart/update alert. Tests verify unchanged output locks and buffer lifetime,
repeated multi-display readback while animations continue on pixman and GLES,
image colors/crops, malformed geometry, save/clipboard failures and cancellation.
HiDPI captures retain pixel resolution, and all eight output rotation/mirror
transforms are covered by exact pixel assertions. The compositor and native
client build successfully; all seven focused snapshot, startup, launch, window
sound and Finder selection suites pass.
The affected Firefox/YouTube reproduction still needs installed-system testing;
the replacement does not establish the root cause of the earlier HTTP 403s.

**Original window-sound live check**: ZacOS Original is available in installed
0.1.14 and already maps drag, release, collapse and expansion events. Enabling
the per-user sound set exposed stalled playback on the developer machine's
HDMI sink: bounded pw-play/paplay previews do not finish, while an original
chirp plays successfully on analog without changing the HDMI default. Resetting
the HDMI sink and restarting user audio services did not recover it; the
existing Haswell position_fix=1 is active. Window-sound, playback and settings
regression suites pass. On installed 0.1.15, YouTube resumed after moving its
stream to analog while HDMI hardware pointers remained frozen. A local HDMI-only
48 kHz/1024-frame period/8192-frame headroom/no-suspend rule still froze when a
second stream joined. Adding interrupt scheduling and a fixed 1024-frame quantum
passed a sustained-stream plus twelve-short-sounds test, but failed the subsequent
live window-sound test: TV audio was silent, hardware progress stopped, and a
woodblock player remained stuck. The unsuccessful local rule was removed and
user audio services restarted; interface sounds remain temporarily disabled at
the user's request, with volume 5 preserved. No HDMI fix is established or
shipped; recovery, audible concurrent playback, and a bounded sound-player
lifetime remain unresolved.
Later checks in that boot, with interface sounds off, saw PipeWire HDMI
streams freeze within seconds of starting, and unbinding and rebinding the
HDMI controller did not recover it; a reboot did. The kernel log's only
audio event was "IRQ timing workaround is activated for card #0" during the
interrupt-scheduled testing, with no i915 FIFO underrun, flip timeout or
codec timeout. Two test tools from that session were unsound and their
results are void: a raw ALSA client wrote 16-bit samples, and a probe
misparsed the padded keys in `/proc/asound/.../status`. After the reboot,
16-bit streams opened directly on the HDMI device stall every time while
32-bit streams play; PipeWire always drives the HDMI sink in S32LE, so this
quirk is not reached in normal use. On healthy HDMI, five minutes of the
drag loop's pattern (clips respawned, SIGKILLed on release, end sounds)
beside a sustained stream, and 57 drag bursts waking an idle sink, nine from
a runtime-suspended controller, all played without a stall. On the analog
sink a clip joining or being killed left the device's trigger time and
hw_params unchanged. The sound pattern alone does not reproduce the hang;
a live test with YouTube, real window drags and interface sounds on, under
a read-only hardware-position monitor, is next.

**Recurring HDMI stall after updating to 0.1.26** (2026-10-06): the
installed Haswell position fix was active and the ACPI storming GPE was
masked, but HDMI's ALSA hardware and application pointers stopped advancing
while Brave and a short imported button-click player remained active.
Terminating that stuck player did not restore hardware progress. Apt history
shows the update installed only ZacOS 9, not an audio-server, browser or kernel
update; this does not establish what triggered the stall. After an authorized
reboot, the user confirmed YouTube video and HDMI/TV sound worked again.
Three bounded original chirps and twelve bounded copies of the previously
stuck button-click sample completed successfully, the latter alongside Brave
playback with advancing hardware pointers. Reopening Software Update, checking
for updates and moving its window also left playback working, confirmed by
the user. HDMI selection and interface-sound settings were unchanged. No
preventative fix was established in that boot; the recurring stall and
unbounded detached sound-player lifetime remained unresolved.

**HDMI freeze recovery without a restart** (0.1.29, 2026-10-07): a
once-a-second monitor caught two HDMI freezes on the iMac14,1 during
Firefox/YouTube, 90 seconds apart. Each time, PCM state and sink stayed
RUNNING with `hw_ptr` frozen, while the TV's ELD, the connector and the
controller's runtime power stayed good. HDA power saving had been turned
off at run time (`power_save=0`, `power/control=on`), so runtime suspend is
ruled out; the power-saving modprobe change tried earlier that day was
dropped unshipped. Suspending and resuming the HDMI sink (`pactl
suspend-sink`) restored TV sound both times, confirmed by the user, with
no reboot. Switching outputs failed because a stuck interface-sound
`pw-play` kept the frozen device open. New `zacos9-hdmi-watchdog` user
service: on Haswell HDMI controllers only, it restarts the controller's
PipeWire sinks after 3 s without hardware progress; it caught and recovered
the second freeze live. `hardware-quirks` covers it (17 tests). Ruled out
since: the IOMMU. Kernel logs (exported by the user) had no audio or
graphics errors at the freezes. With the HDMI controller's IOMMU group
switched live from `DMA-FQ` to `identity` at 15:09 (`position_fix=1` kept),
it ran 20 minutes clean and then froze again at 15:28:56; the watchdog
recovered it. An `identity` change briefly added to `zacos9-hda-load` was
reverted unshipped. The root cause of the freeze is still unknown, and so
is the unbounded detached sound-player lifetime.

**USB erase safety-lock fix and disk names** (0.1.14): The helper's own
exclusive disk claim prevented sfdisk from taking another exclusive claim,
producing an erroneous "disk is currently in use" failure on unmounted disks.
sfdisk now leaves the preflight claim and kernel refresh to the helper: the
helper retains its exclusive descriptor, verifies the written table, fsyncs
and performs BLKRRPART through that same descriptor. No force option is used;
refresh failure stops before mkfs and reports that the partition table changed.
Both Erase Disk and unreadable-disk Initialize request a FAT32 label before
confirmation, defaulting to Untitled. Cancel causes no unmount or erase. Names
are validated by both GUI and privileged helper and passed to mkfs as one
argument. Raw blkid verification preserves spaces instead of comparing escaped
export labels. Physical disk/privileged-loop verification remains pending.
Finder and menu bar build, and all five focused suites pass. The helper suite
has 71 tests, including actual mkfs/blkid round-trips on disposable regular-file
images (no physical storage), exclusive-descriptor refresh and busy-refresh
failure tests. Offscreen dialog tests cover editing, invalid names and Cancel;
controller tests cover label propagation and cancellation in both entry points.

**Special > Erase Disk** (0.1.13): Select one safe mounted USB disk icon
to enable Erase Disk. A default-Cancel confirmation identifies the physical disk
and warns that every partition will be erased. Administrator authorization,
identity revalidation and non-forced unmounting precede the existing verified
FAT32/MBR formatter. Finder inhibits automount during the operation, then
remounts Untitled. Internal/system, encrypted, read-only and unsafe mount
topologies remain excluded; automatic unreadable-disk initialization still
rejects mounted disks. Hardware-free menu, controller and hot-plug regression
tests cover gating, cancellation, replacement and automount inhibition. No
physical disk was erased; interactive hardware verification is pending.
Finder and menu bar build successfully. All five focused suites pass, including
66 mock-only helper tests and invalid-success/authorization-error controller
checks. The menu-state parser remains compatible with older Finder messages.

**USB hot-plug discovery fixes** (0.1.12): Finder now services GLib/GIO
events with a bounded Qt timer, handles volume-changed when an inserted drive
becomes mountable after volume-added, and registers listeners before startup
enumeration. Mounted storage under /run/media is visible rather than filtered
as runtime storage. Disk-initialization failure reporting starts before desktop
automount, and completed mount requests refresh disk icons. Hardware-free
hot-plug tests pass with Qt GLib integration disabled, covering late readiness,
reinsertion, failure reporting and path filtering. Finder builds and disk-dialog
and selection regression tests pass. Physical hot-plug verification remains
pending on the installed update.

**Unreadable USB disk initialization** (0.1.11): Sniffer checks inserted
USB storage and mount failures and offers a Platinum Eject/Ignore/Initialize
alert. Initialization requires a second explicit whole-disk erase confirmation
with Cancel as default, followed by administrator authorization. A constrained
helper creates one FAT32 partition labeled Untitled, rejecting unsafe storage
and verifying device identity again before destructive commands. Mounted,
system, encrypted and read-only devices are not initialization candidates.
Physical USB and formatting validation are pending; no real disk is erased
during automated tests.
Finder builds successfully. The 40 mocked helper tests, offscreen dialog
controller tests and Finder selection regression suite pass. Helper tests cover
mounted/system-device topology, swap, protected signatures, identity changes,
exclusive opens and verified FAT32 command sequencing. Read-only discovery was
checked; physical USB formatting verification remains pending.

**HDMI audio on Haswell Macs** (0.1.8): HDMI/DisplayPort sound stuttered
on the iMac14,1 while analog played cleanly. The Haswell HDMI controller's
position buffer runs ahead of the sound sent: its pointer leapt 500-2,400
frames in 1-2 ms and overtook PipeWire about 14 times a second ("Unstable
LPIB" in the kernel log). PipeWire buffering and interrupt scheduling still
underran. A modprobe hook (`hardware/zacos9-hda-load`) loads `snd_hda_intel`
with `position_fix=1` (LPIB) for Haswell HDMI controllers only, as upstream
already does for Broadwell. `zacos9-gpe-guard.timer` masks ACPI GPEs firing
over 1,000/s on Macs: GPE 0x06, the graphics SCI, fired ~24,000/s from boot
and kept `irq/9-acpi` on most of a CPU. `hardware-quirks` covers both against
a fake /sys. Confirmed on the iMac: HDMI playback is clean after a restart.

**Appearance presets and sound themes** (uncommitted): Appearance adds Themes
and Sound Sets tabs with saved preset combinations, None/original/custom sound
sets, preview and independent interface volume. Registry version 8 adds Themes
and Sound Themes under Appearance. Interface events are wired into ZacOS
buttons/checkboxes, menus, Sniffer windows and successful Trash operations.
Classic PCM sound resources and WAV manifests import without modifying sources;
full classic window/control skins are intentionally unsupported for this phase.
The local compatibility archive is excluded from commits/package builds.
Full build and nine focused suites pass. The local sample archive validates
496 entries and 374 resource forks; all 75 sound-set metadata maps were checked,
with 399 supported PCM events decoded in private verification. Unsupported
compressed samples and command sequences are reported. UI rendering and
playback volume/overlap are covered by offscreen tests. Not installed/published.
StuffIt extraction creates visible companions for resource-only files; Sniffer
preserves their AppleDouble forks during individual moves and copies. The sound
catalog also accepts a copied Sounds folder with one level of classic sets.

**Window interaction sounds** (0.1.10): sound sets now accept
window-collapse, window-expand, window-drag and optional window-drag-end.
The compositor emits collapse/expand on actual state changes and starts
repeating drag playback only after movement. Release stops playback; Escape,
window unmap, fullscreen transitions and session shutdown cancel it. Classic
PCM events wcol, wexp and wmov are imported; drag repeats the complete sample,
not classic interactive variants or embedded loop regions. No Apple audio is
bundled. The release also requires the StuffIt decoder at runtime.
Compositor and Appearance builds pass; focused state-machine, mock playback,
classic importer, offscreen theme-selection and settings suites pass. Physical
audio and interactive desktop verification are still pending.

**Expanded optional Software catalogs** (uncommitted): Featured stays the
default; searchable All Applications uses Debian AppStream desktop metadata,
and optional Flathub is enabled explicitly for the current user through
Additional Sources. Flatpak app installs/removals are user-scoped, and disabling
the source retains apps/data. Metadata discovery is asynchronous with timeout
and explicit errors; arbitrary APT sources are excluded. Session data paths
include Flatpak exports for Sniffer/menu launchers. Mocked catalog and offscreen
browser tests exercise discovery, filtering, confirmation and command routing;
no sources or applications have been added to the running system.
The full build and five focused suites pass, including the pending wallpaper
folder migration. Not installed or published yet.

**Wallpaper folder placement** (uncommitted): Wallpaper now lives inside
System Folder > Appearance, backed by `zacos9/appearance/Wallpaper`.
Registry version 7 reparents the old node and preserves overrides; existing
wallpaper storage is moved without changing image IDs. Failed moves are logged
and retain the old storage rather than losing or overwriting user files.
Sniffer and Appearance build successfully; the catalog, VFS and selection
test suites pass, including legacy file moves and registry override preservation.
Not yet installed or published.

**Sniffer multiple selection** (0.1.7): desktop, icon and list views
support Mac OS-style dotted selection rectangles, Shift-click and Shift-drag
toggling, and Edit > Select All / Command-A. List rectangles start in blank
columns, select rows by icon/name and autoscroll at window edges. Existing
multi-item drags move or Option-copy the whole selection. `finder-selection`
uses real offscreen mouse/key events to cover icon and list rectangles,
toggling, Select All, autoscroll and a three-file drop into another folder.
Desktop rectangle code shares the tested hit-area/painting helpers; physical
desktop validation is pending.

**Desktop wallpaper mode** (0.1.6): a separate System Folder > Appearance > Wallpaper
folder and Appearance Wallpaper tab allow user photos and classic PICT pictures.
Choosing a pattern or photo switches background mode while retaining both
selections. Fit, Fill, Stretch and Center render independently on each monitor.
An asynchronous photo catalog shares the existing importer, with photo-sized
limits and cached per-monitor renderings invalidated on file replacement.
Focused tests cover placement extents, central cropping versus stretching,
landscape/portrait monitor geometry, selection persistence, mode switching,
replacement cache invalidation and classic PICT imports. Installation on the
physical desktop is pending.

**User-created desktop patterns** (0.1.6): System Folder > Appearance >
Desktop Patterns maps to the user's XDG data directory. The Appearance Desktop
tab, main desktop and secondary monitors share an asynchronous pattern catalog
that discovers additions, replacements and removals every two seconds.
Normal image tiles and classic Mac ppat/PAT/PAT#/ppt#/PICT resources are read
from raw forks, MacBinary, AppleDouble and checksum-validated BinHex; standalone
PICT uses the packaged ImageMagick converter. Original files are unchanged,
selection IDs are stable, built-in patterns remain available, and errors are
shown in the panel tooltip and logged. Unsupported pattern packing/layouts
are explicitly rejected. Synthetic tests contain only original artwork.
Focused tests pass for format decoding, palette/direct pixels, BinHex checksums,
file discovery/replacement/removal, stable selection persistence and an exact
112x112 custom-tile preview. The writable System Folder path and existing
zip/7z/tar extraction tests pass. Physical desktop installation is pending.

**Animated dog busy pointer** (0.1.6): original monochrome cursor artwork
wags its tail and backflips for standard wait/progress requests, including
Wayland cursor-shape requests and themed Wayland/Xwayland cursors. Sniffer's
desktop entries, built-in launchers, document opening and application menu
send launch feedback through platinum-shell-v1 version 3. Matching app IDs,
launch PIDs or their descendants end feedback when a window maps/activates;
failure, launcher disconnection and a 15-second timeout prevent stuck feedback.
The normal client cursor is preserved during launch feedback, and pointer input
continues normally. Document launches use the registered GIO handler so its
identity is tracked instead of an already-exited xdg-open wrapper. Classic's
pre-launch check is asynchronous so it does not freeze pointer feedback.
Xwayland also publishes cursor X resources for Qt's XCB theme lookup.
Custom application-drawn busy cursors cannot be identified.
Focused cursor artwork, launch lifecycle and isolated Wayland/Xwayland pixel
checks pass, including complete animation cycles, wait/progress aliases,
normal/text cursor restoration and the exact 15-second timeout. The affected
desktop components build successfully; physical desktop validation is pending.

**Sniffer naming** (0.1.5): user-visible Finder branding is now Sniffer,
including the application menu and Hide command, Qt application name, folder
desktop entry, login-session description and Windows installer guidance.
Executable names, application IDs, D-Bus interfaces and source paths remain
unchanged for compatibility.

**Application fullscreen** (0.1.5): the compositor now honors native
Wayland, Xwayland and foreign-toplevel fullscreen requests instead of ignoring
them. Fullscreen removes Platinum decorations, fills one output including the
menu-bar area, and restores the previous frame position/size and zoom state on
exit. Active fullscreen windows sit above the menu bar; focusing a regular
window returns fullscreen content to the normal view layer. Output changes
refresh fullscreen geometry and disconnected outputs fall back to a remaining
display. `fullscreen-protocols` exercises fullscreen enter/exit with actual Qt
Wayland and Xwayland clients in an isolated dual-output headless compositor.
Captured native Wayland frames confirm content fills every pixel of the selected
1280x720 output and restores its exact previous rendered position and size.
Firefox/YouTube on the physical desktop has not yet been retested after restart.

**Volume sound feedback** (0.1.5): successful system-volume adjustments
preview the selected alert sound via `pl_beep()`, once on drag release or after
a keyboard change. Playback waits for the audio-server command to succeed;
failed commands do not produce success feedback. Existing mute, alert-volume
and None-sound behavior is retained.

**Multi-monitor boot/startup artwork** (0.1.4): Plymouth renders a separate
centered logo, progress bar and password prompt in each display viewport.
The compositor no longer stretches one startup buffer across the combined
desktop: each output has its own centered logo/Welcome box, visible only during
that output's frame commit, including overlapping mirrored layouts. Hot-plug,
different resolutions, negative layout positions and HiDPI are covered by
`startup-displays`; `boot-splash-displays` executes the theme in Plymouth's real
interpreter with display mocks. Actual isolated dual-headless compositor frames
were captured and checked for centered logos. Packages activate Debian's
`update-initramfs` trigger so theme upgrades reach the next boot. GRUB's earlier
display behavior remains firmware-dependent. No live dual-monitor reboot tested.

**Sound output selection** (0.1.3): the Sound control panel has a Platinum
output menu for built-in speakers, available HDMI/DisplayPort monitor outputs,
headphones and other audio-server outputs, including Bluetooth. Names include
the monitor name when provided. Selection sets the default sink/port and moves
existing playback streams; volume and mute track the active output. Asynchronous
`pactl` calls have timeouts and visible errors; the output list refreshes every two
seconds for hot-plug and server fallback. Focused `sound-output` tests cover
discovery, routing, ports, volume/mute, disconnect/reconnect and error paths.
Read-only live discovery confirmed built-in speakers and the connected SONY TV.

Resuming the work on other applications' look (themes, styles, global menus): see `docs/app-integration.md`.

**balenaEtcher preinstalled, in Applications > Utilities; a polkit password prompt** (package built and inspected; Utilities
covered by `test-finder-vfs`; not yet installed or run - needs root and a new ISO).

- `packaging/etcher/build-deb.sh` -> `balena-etcher_2.1.7_amd64.deb` from balena's Linux zip (SHA-256 pinned to GitHub's digest;
  the user's ~/Downloads copy matched), in /usr/lib/balena-etcher (Debian's AppArmor profile path), setuid chrome-sandbox, icon
  from balena's repo, desktop entry `Categories=Utility;X-ZacOS9-Utility;`. ISO installs it; installer Update carries it;
  `zacos9` Recommends it.
- Applications > Utilities (`applications/utilities`, registry v4; v3 registries gain it and Applications' exclusion on upgrade)
  shows X-ZacOS9-Utility entries, even ones the system came with (`appdb.cpp` exempts them from base-applications).
- Etcher writes disks as root through `pkexec /bin/bash -c ...`: no safe polkit rule can allow that without a password, so
  `zacos9` now depends on `lxqt-policykit` and the session starts `lxqt-policykit-agent` (a Qt 6 password dialog, so Platinum).
- Not done: Software Update only fetches the `zacos9` .deb, so existing systems get Etcher from a new ISO's Update or by hand.

**Double-clicking an archive expands it in place** (built; checked in a nested session; 13 checks in `meson test expand`).

- StuffIt Expander's way: a double-clicked .zip / .7z / .sit / .tar / .tar.gz / .tgz / .tar.bz2 / .tar.xz is expanded beside itself by
  `zacos9-expand` (`shell/finder/`, Python's zipfile/tarfile and python3-libarchive-c for 7z). One top-level item lands as itself, several go in
  a folder named after the archive; taken names get " 2"; the archive is kept; the result is selected. A shared status dialog appears while
  any supported archive is expanding. Entries escaping the folder
  are refused, __MACOSX/._ files skipped, zip Unix modes kept, password-protected zips refused with a message.
- Seen: Photos.zip on the desktop -> "Photos" selected beside it; Project.zip in a Downloads window -> "Project" selected.
- StuffIt runtime packaging fix (0.1.10): `unar` is now a mandatory
  runtime dependency, not just a build dependency, so installed systems receive
  both `unar` and `lsar` for `.sit` double-click extraction. Resource forks are
  preserved as AppleDouble companions; existing extraction tests use generated
  StuffIt files to cover in-place output, uppercase extensions and collisions.
- 7z support (0.1.6) follows the same naming, collision avoidance,
  original-archive retention and partial-extraction cleanup. Links, special
  entries and unsafe paths are refused; encrypted or damaged archives show an
  error. Debian installs the libarchive Python reader as a runtime dependency.
  Focused extraction tests cover real 7z contents, executable permissions,
  single/multiple roots, repeated extraction, uppercase extensions, metadata
  filtering, traversal, links, empty/corrupt archives and encryption, alongside
  the existing zip/tar behavior.
- Not done: a progress window for big archives (it works in the background; the result appears when done); .rar.

**Other programs' "Show in Folder" opens the Finder** (built; checked in a nested session).

- Reported on real hardware: Firefox's folder button after a download did nothing. Nothing on the session bus was
  `org.freedesktop.FileManager1`, and nothing was the default for `inode/directory`, so its fallback failed too.
- The Finder now owns `org.freedesktop.FileManager1` (`shell/finder/filemanager1.cpp`): ShowFolders opens folder windows,
  ShowItems opens the item's folder with it selected, ShowItemProperties adds Get Info. A hidden `zacos9-finder.desktop`
  (`zacos9-finder --open URI...`, forwarded to the running Finder) is the default for folders through
  `share/xdg/zacos9-mimeapps.list` (read via XDG_CONFIG_DIRS and XDG_CURRENT_DESKTOP=ZacOS9).
- Seen: gdbus ShowItems on Downloads/report.pdf -> Downloads window, report.pdf selected; `gio open ~/Documents` -> its window.

**Icon labels: cut in the middle, whole when selected** (built; seen in a nested session).

- As Mac OS 8/9: in icon views and on the desktop a long name is cut in the middle to 72 px ("Mousep…rences", "Quarter… 3.txt")
  so it fits its 80-px grid cell; a selected icon shows its whole name (up to 400 px), drawn after everything else so it lies over
  its neighbours, and moved sideways to stay inside the screen or window. Button view shows the whole name when selected too.
  `Item::labelText()` / `fullLabelText()` / `shownLabel()` in `shell/finder/items.cpp`; `paintIconLabel` for the second pass.

**Every drive on the desktop at startup** (built; seen working on the real system with the build's Finder).

- Reported on real hardware: other internal drives (NTFS "Archive and Projects", "GAMING SSD", a Windows partition) didn't appear on
  the desktop: the Finder showed mounted volumes only, and nothing mounts internal drives (udisks: should_automount=0).
- `localVolumesMountAll()` (`shell/finder/localvolumes.cpp`): at Finder start, every GVolume with a block device that can mount and
  isn't mounted gets `g_volume_mount`; later ones too (volume-added). Each is tried once; failures are logged. GIO already hides
  EFI/recovery partitions. `share/polkit/50-zacos9-mount.rules` grants udisks2 filesystem-mount(-system) to local active sudo users
  (internal drives otherwise need a password, and nothing here can ask for one).
- Seen for real once the rule was installed (Finder run from the build): all three mounted under /media/adam and on the desktop.
- Also fixed: `Desktop::paintEvent` never drew `m_localVolumes` (since local volumes were added): they had places and could be
  clicked, but were invisible.

**Installer: Update an installed ZacOS 9 instead of erasing** (built; window flow checked in a nested session with a fake helper;
`--detect`/`--update` not yet run for real - needs root, a new ISO and a disk with ZacOS 9, e.g. the iMac).

- `zacos9-install --detect` (read-only mounts) finds installed systems; the installer lists "— ZacOS 9 0.1.0" beside such a disk and
  offers Update (default; Reinstall at the same version, none if the disk's is newer) beside Erase & Install.
- `zacos9-install --update PARTITION`: apt installs the medium's `zacos9/*.deb` into the system (new dependencies from Debian, so
  online if it has any), then Debian's updates, then update-grub; accounts, files, settings, fstab and boot loader kept.
  `scripts/build-iso.sh` puts the packages on the medium. See `docs/updates.md`.
- Fixed on the way: the script's cleanup did `rm -rf` on the mount point after `umount -R`, which would have deleted a disk's files
  if an unmount ever failed; it is `rmdir` now (start, cleanup and end).

**Bluetooth control panel; headphones and speakers as the sound output** (built; window seen in a nested session; not tried with
real devices yet - this machine has an adapter, but bluez wasn't installed).

- `zacos9-bluetooth` (`shell/panels/bluetooth.cpp`), Apple menu > Control Panels > Bluetooth: Bluetooth On, the device list, Search,
  Connect/Disconnect (pairs and trusts first if needed), Forget, Play Sound Here. BlueZ over the system D-Bus (QtDBus; a
  NoInputNoOutput pairing agent while open); an audio device's PipeWire sink becomes the default output through pactl.
  `debian/control` depends on bluez. See `docs/bluetooth.md`.

**Screen Snapshot in the Apple menu** (built; checked in a nested session; not released yet).

- `shell/snapshot/zacos9-snapshot`: drag a rectangle (slurp, Platinum-styled: black 1-px outline, screen lightly dimmed; Escape
  cancels); grim saves it on the Desktop as "Picture N.png" (Mac OS 9's naming) and wl-copy puts it on the clipboard.
- zacos9-wm now offers `wlr-data-control` so wl-copy sets the clipboard without a window of its own. `debian/control` depends on
  grim, slurp, wl-clipboard.
- Seen: Apple menu > Screen Snapshot, a 300x200 drag -> "Picture 1.png" (300x200, no overlay in it) and `image/png` on the clipboard.

**Nested zacos9-wm overwrote the real session's screens file** (fixed; verified).

- `prefs_write_outputs` named `$XDG_RUNTIME_DIR/zacos9-outputs-*` after `$WAYLAND_DISPLAY`, but runs as the outputs appear, before
  zacos9-wm sets that to its own socket: a nested session wrote the outer session's file. The real Finder then read a nested screen
  ("WL-2") as the main display, put its pattern-only `desktop-extra` layer over both real screens, and hid every desktop icon.
  Now named after `server->socket`. Checked: a nested run leaves the real file byte-identical and writes `...-wayland-1`.

**Clicking the desktop puts the Finder in front** (built; checked in a nested session).

- As on the Mac: a click on the desktop (`zacos9-desktop`, or `desktop-extra` on other displays) brings the Finder's windows forward
  above every other program's, in their own order (and back from Hide Finder), and leaves no window active
  (`view_clear_focus` in `compositor/src/view.c`), so the menu bar shows the Finder's menus (File, Edit, View, Special; "Finder" at
  the right). Clicking a window makes it active again. Seen in a nested session: foot in front of a Finder window -> desktop click ->
  the Finder window comes above foot, Finder's menus, foot's frame inactive; foot alone -> desktop click -> Finder's menus.

**New Tricks in the Apple menu** (built; window seen in a nested session; installing not tried yet - needs a published release).

- `zacos9-update` (`shell/update/`): checks the newest GitHub release's `zacos9_*.deb` against the installed version and apt's pending
  Debian updates, lists both, and with Update downloads, checks (digest, package name, version) and installs the .deb, then Debian's
  updates, through `zacos9-appstore-helper` (new `refresh` and `upgrade` commands). Offers Restart after a ZacOS update.
- `scripts/release.sh VERSION "notes"`: changelog entry, tag, build, push, `gh release create` with the .deb. See `docs/updates.md`.
- Seen: "Updates are available" (a test release from a file) and "up to date" (the real repository, no releases yet).
  19 checks in `meson test update-check`.

**Global menus for Qt 6 programs** (built; tested in a nested session; not on the installed system yet).

- A Qt 6 program's own menus show in the menu bar while its window is in front, and its in-window menu bar goes away.
  `shell/menubar/appmenu.c` (registrar + dbusmenu over GDBus); zacos9-wm reports the front window's pid
  (`platinum_shell_v1` version 2). Details, what's shown and what isn't: `docs/app-integration.md`.

**Terminal: ⌘C/⌘V copy and paste** (built, installed as a .deb and confirmed working on the real system 2026-10-04).

- Reported on real hardware: copy/paste doesn't work in Terminal. zacos9-wm turned ⌘ into Ctrl for every client, so in foot ⌘C was an
  interrupt and ⌘V an unbound Ctrl+V (foot's own keys are Ctrl+Shift+C/V).
- zacos9-wm now leaves ⌘ as Super (Mod4) when foot has the keyboard (`keeps_command_key` in `compositor/src/input.c`); other apps unchanged.
  The menu bar's Edit commands now send ⌘ rather than Ctrl, so zacos9-wm makes the same choice for them.
- `share/xdg/foot/foot.ini` (installed to /usr/share/zacos9/xdg, put first in XDG_CONFIG_DIRS by zacos9-session) binds Mod4+c/v to copy
  and paste. Checked: foot 1.21 reads it from there (`foot --check-config`); ~/.config/foot/foot.ini still overrides it.
- Confirmed by the user after logging in to the installed build: ⌘C/⌘V copy and paste in Terminal.
- Seen in the log, not done: zacos9-wm has no primary selection (middle-click paste), which foot warns about.

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

**iMac freezes (joining Wi-Fi, dropping an app on Applications): GPU lockups - fix built, not committed.**

- The user's `journalctl -b -1 -k` from a frozen boot (iMac7,1, Radeon HD 2600, `radeon` driver): CP stalled status registers, "GPU reset
  succeeded, trying to resume", then a ttm_bo_release warning - the GPU's 3D engine hung under zacos9-wm's GLES drawing and the screen never
  came back. Not our panels; joining from Terminal only worked because nothing new was drawn.
- `zacos9-session` now uses the pixman renderer when the GPU's driver is `radeon` (pre-GCN Radeons); amdgpu cards keep GL.
  The xdg-activation guards added while hunting this (mapped-only focus, no double insert) stay.
- Seen in the same log: Wi-Fi is Broadcom's proprietary `wl` module (taints the kernel, works).

**Control panel changes ignored in a fresh account - the real cause of the Monitors revert** (fixed; reproduced and verified; not committed).

- zacos9-wm watches ~/.config/zacos9 for the panels' changes, but made it with a one-level mkdir(): in a new account (every live session, and
  the first login after Setup) ~/.config doesn't exist yet when zacos9-wm starts, so the watch failed silently and no panel change applied
  for that whole session (the Finder makes the folder a moment later, so the panels' writes did land in the file). Monitors then re-read the
  unchanged layout and the display jumped back. Mouse, keyboard and resolution changes were equally ignored.
- Now mkdir -p in prefs_init and in lib/settings (pl_setting_set), and a failed watch is logged. Reproduced with HOME=/tmp/fh (empty) and two
  nested screens: before, desktop.conf held the new place but WL-2 stayed at 1280,0; after, WL-2 moved to -30,720.

**Monitors Arrangement: grab offset bug** (found by reproducing; fixed; not committed).

- Real-hardware report: arranging a display reverted and nothing was saved. Cause: on pressing a display that was already selected, the grab
  offset was measured against a stale position (`m_dragPos`, left by the previous drag or never set), so the drop snapped somewhere far off,
  most often back to the start. The first display is selected from the start, so dragging it was always wrong unless it sat at the origin.
- Test (two nested screens, display 1 at x=1280, panel on the main one): dragging display 1 below display 2 now saves and applies (x -30, y 720).

**Monitors Arrangement: drag preview** (built; seen in a nested session; the reported spring-back was not reproduced; not committed).

- Reported on real hardware: dragged displays spring back to where they were. In nested sessions saved layouts always stuck (side, below, raised,
  different sizes - checked by writing desktop.conf directly). Likely cause: the dragged display did not move on screen and snapped to the *nearest*
  touching place on release, so a short drag landed where it started. Now the display follows the pointer and an outline shows where it will land.
- The compositor logs "display layout: the saved places overlap..." (session.log) if it ever discards a saved layout.

**TCP/IP panel: hardening after a crash choosing a Wi-Fi network on the iMac** (built; panel starts; the Wi-Fi path itself is untested - no NetworkManager/Wi-Fi in the test setup; not committed).

- Cause not found. Fixed what could plausibly do it: `nm::run` used `QProcess::waitForFinished()` (banned in CLAUDE.md - it deadlocks in this
  Qt/Wayland process) on the GUI thread for every refresh, join and scan; it now serves events while waiting. The 4-second refresh can no longer
  run inside itself, and join/choose guard a device or list that changed while a dialog was open.
- Other `waitForFinished` calls remain elsewhere (controlpanel, datetime, filesharing, netvolumes, smbclient, discovery, appdb, finder) - not touched.

**Installer lists whole drives only, by make and model** (built; seen on screen; not committed; not in an ISO yet).

- `shell/installer`: the destination list shows only whole disks, each by its make and model (from `lsblk`) - no size, no device name,
  no partitions; drives that report no model are "Hard Disk"; two of one make are numbered (1), (2). The warning names the disk.
  `zacos9-install` itself still accepts a partition on the command line; the window no longer offers one.
- The drive ZacOS 9 is running from (the installer USB or disc, found through findmnt on /run/live/medium) is left out of the list.

**Several displays; Monitors panel Arrangement** (built; tested with two nested screens, not on real monitors; not committed; not in an ISO yet).

- Compositor (`prefs.c`, `output.c`, `layers.c`): per-display settings in desktop.conf - `main-display=NAME`, `display.NAME.resolution=WxH`,
  `display.NAME.x|y=N` (layout position, logical pixels), `mirror=1`. Layout is applied from them (side by side if none or overlapping);
  ZacOS 9's own layer surfaces (namespace `zacos9-*`: menu bar, its menus, desktop icons) are pinned to the main display and move with it
  live, also when a screen is unplugged. The desktop colour rect now reaches negative coordinates. The screens file gains `x y main`, `mirror`.
- Monitors panel: with more than one display, an Arrangement box (drag a display; it snaps to touch another; drag the menu bar strip to
  another display to make it main), Mirror Displays, and the Resolution list for the selected display. One display looks as before.
  Pixel size (scale) is still one setting for all displays.
- Finder: `SecondaryDesktop` shows the desktop pattern on every non-main display.
- Seen with WLR_WL_OUTPUTS=2: dragging display 2 below 1 saved x/y; dragging the strip made display 2 main (menu bar and icons moved, windows stayed);
  mirror put both at 0,0; display.WL-2.resolution=1024x768 applied. Not tested: real monitors/hotplug, dragging windows between displays.
- Not done: per-display pixel size, a different pattern per display, mirror with different resolutions (the smaller shows part).

**Qt style plugin** (built; seen on screen with a Qt 6 test program; not committed; not in an ISO yet).

- `shell/qtstyle/`: a `QProxyStyle` over Fusion, installed to `<libdir>/qt6/plugins/styles/zacos9style.so`, selected for every Qt
  program in the session by `QT_STYLE_OVERRIDE=zacos9`. Platinum palette; push buttons (incl. default ring), check boxes, radio
  buttons, text field frames and scroll bars are drawn by `lib/widgets.c`, so they match the native programs pixel for pixel.
  Scroll bar geometry (arrows at both ends, fixed thumb) is defined in `subControlRect` to match `lib/widgets.c`.
- Seen: buttons, disabled button, check boxes, radios, line edit, a scrolling text view with the Platinum scroll bar. Thumb dragging and
  the arrows were driven with the pointer and scroll the text; page clicks weren't tried. Menus, tabs, sliders, combo boxes, spin boxes are Fusion in the Platinum palette.
- Qt 6 only (Debian 13's Qt 5 programs are not covered). Accent colour is read at start-up from the Appearance panel's setting.

**GTK 3 Platinum theme; toolkit session defaults** (built; looked at on screen with Mousepad; not committed; not in an ISO yet).

- `share/themes/ZacOS9/gtk-3.0/gtk.css` (installed to `/usr/share/themes`): Adwaita as the base, restyled - grey 3-D outlined
  buttons, square corners, sunk white fields, flat grey menu bar and menus, lavender selection, Platinum-style scroll bars and tabs,
  no shadows or animations. `zacos9-session` sets `GTK_THEME=ZacOS9`, `GTK_CSD=0`, `QT_WAYLAND_DISABLE_WINDOWDECORATION=1`.
- Seen on screen in Mousepad (GTK 3): grey menu bar, an opened File menu (grey, lavender bar highlight), the Preferences dialog
  (outlined buttons, tabs, square check boxes with a black mark, combo box). Not seen: scroll bars, GIMP/Inkscape
  (GIMP keeps its own dark theme until Preferences > Theme is set to System). Lavender is fixed - the Appearance accent isn't followed yet.
- Not done: Qt style, GTK 4/libadwaita (ignores themes), global menus (next).
- Build tree: the theme is only installed, not copied into build/share; point XDG_DATA_DIRS at share/ to try it from a build.

**Windows come forward when re-opened; double-click opens one item** (built; checked in a nested session; not committed).

- Compositor: `xdg_activation_v1` (`compositor/src/xdg.c`) - a client asking for its window to be activated brings it to the
  front with keyboard focus. Qt's `activateWindow()` (used by `FolderWindow::open` for an already-open folder) uses it, so
  opening the startup disk while its window is buried raises it. If none is open, one opens as before.
- Double-click opens only the item under the pointer (selecting just it), on the desktop and in folder windows, so two
  icons selected by accident (shift-click, marquee) aren't both opened. File > Open still opens every selected item.

**Documents replaced by Home; the user can add folders to the startup disk** (built; VFS test passes; not committed). Registry version 3 drops the untouched default Documents node (Home holds ~/Documents). File > New Folder in the disk's window makes a folder of the user's own (`vfsNewFolder`, registry node with `"user": true`, real directory in `~/.local/share/zacos9/finder/folders/`); renamable, takes files, Move To Trash deletes it only when empty.

**Home folder on the startup disk; Wine programs without a menu entry** (built; VFS test passes; not tested with a real Wine install; not committed; not in an ISO yet).

- The startup disk gains **Home** (-> `~`, so Downloads from Firefox and everything else is reachable). Registry
  version 2: existing registries pick it up by migration.
- Reported: a program installed through the Windows Installer (notepad .exe) didn't appear in Applications.
  Cause not confirmed (installer probably filed no Start-menu entry). The installer now notes the program
  folders in the prefix (`Program Files*`, `AppData/Local/Programs`) and, if it finds no new Wine entry but a
  new folder, writes its own entry in `~/.local/share/applications/wine/Programs/` for the folder's main .exe.

**Software window: Inkscape › Illustrator Layout** (built and tested; not yet committed; not in an ISO yet).

- Same button as GIMP's. Inkscape 1.4 already ships `adobe-illustrator-cs2.xml`; the preset (new
  `"kind": "prefs"` in `zacos9-appstyle`, no download) sets `/options/kbshortcuts/shortcutfile` in
  `preferences.xml` and reset restores just that value. Verified with real Inkscape 1.4 (strace shows
  it loading the Illustrator set after apply). 7 more checks in `tests/store/test_appstyle.py`.
- Krita Photoshop preset still not done (Krita 5.2.9; scheme unconfirmed).

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
  *Update (0.1.32):* the second gap now has that hand-off (`docs/boot.md`), pending a real boot.

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
- ~~Long icon labels overlap in icon view~~ - fixed (2026-10-04): labels are cut in the middle to fit their grid cell, and shown
  whole, over their neighbours, while selected (Mac OS 8/9's way).
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

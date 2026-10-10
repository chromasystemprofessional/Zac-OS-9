# CLAUDE.md — ZacOS 9

## Architectural map

| Component | Path | Language |
|---|---|---|
| Wayland compositor | `compositor/` | C (wlroots 0.18) |
| Finder (desktop + windows) | `shell/finder/` | C++ / Qt6 |
| └ Macintosh view (startup disk, Applications) | `shell/finder/vfs.cpp`, `appdb.cpp` | C++ / GIO — see `docs/vfs.md` |
| Menu bar (incl. Apple menu) | `shell/menubar/` | C++ / Qt6 |
| Control panels (Bluetooth: `bluetooth.cpp`, see `docs/bluetooth.md`) | `shell/panels/` | C++ / Qt6 |
| Software window (app catalog over apt) | `shell/store/` | C++ / Qt6 — see `docs/appstore.md` |
| Software Update (ZacOS releases from GitHub + Debian updates) | `shell/update/`, `scripts/release.sh` | C++ / Qt6 — see `docs/updates.md` |
| Network Browser (connect to AFP/SMB servers) | `shell/network/` | C++ / Qt6 — see `docs/network.md` |
| Platinum look for other apps (GTK 3 theme, Qt 6 style; global menus next) | `share/themes/ZacOS9/`, `shell/qtstyle/` | CSS / C++ — see `docs/app-integration.md` |
| Classic (SheepShaver) launcher | `shell/classic/` | sh |
| Session entry | `session/` | sh |
| Boot splash, extension parade, splash→desktop hand-off | `boot/` | Plymouth script + C + sh — see `docs/boot.md` |
| Hardware quirks (Haswell HDMI audio, Mac ACPI interrupt storms) | `hardware/` | sh + modprobe.d + systemd — see `docs/hardware.md` |
| AFP 2.x FUSE client | `network/afp/` | C (libfuse3) |
| File sharing helper + polkit | `sharing/` | sh + policy XML |
| Software helper + polkit | `appstore/` | sh + policy XML |
| Emulation integration | `emulation/` | sh + config |
| Assets (fonts, icons, patterns) | `assets/` | PNG, binary |
| Tests | `tests/` | sh, Python |
| Build helpers | `scripts/` | sh |
| Meson build | `meson.build` + per-dir `meson.build` | Meson |

**Never read or edit:** `build/`, `emulation/SheepShaver/` (upstream), `uploads/`, `macos/` (Apple reference files — do not ship).

## Build & test

```sh
# Build only (run in WSL Debian):
scripts/build.sh

# Quick VM boot for interactive testing (no ISO rebuild):
scripts/dev-boot.sh

# Build the package and install it on this computer (one sudo prompt; then log out/in):
scripts/install-live.sh            # --no-build installs build/packages as is

# AFP client test (against Netatalk on localhost):
AFP_PASSWORD=Sesame-42 tests/sharing/afp-client.sh 127.0.0.1 "platest's home" platest

# AFP client test (against Mac OS 9 via Classic):
AFP_PASSWORD=macpass tests/sharing/afp-client.sh 127.0.0.1:5548 "Macintosh HD" "^Mac User"
```

**Never rebuild the ISO after each code change.** Use `dev-boot.sh` for iteration; the ISO is a packaging step done only when explicitly requested.

## Workflow rules

1. Identify the smallest set of relevant files before reading. Check this map first.
2. Read only the needed sections of large files (`offset`/`limit` on the Read tool).
3. Use `rg` (ripgrep) for symbol searches; exclude `build/`, `emulation/SheepShaver/`, `uploads/`, `macos/`, `tools/measure/figs/`.
4. Redirect long build output: `ninja -C build >build/build.log 2>&1; tail -20 build/build.log`.
5. One logical feature per session. No unrelated cleanup.
6. For changes touching >3 files, write a short plan first, then implement.
7. Run focused tests before broader ones.
8. Update `PROJECT_STATUS.md` when finishing a task.

## Key constraints

- **No Apple assets.** No Happy Mac, no startup chime, no Apple fonts or icons.
- **Happy Zac** (`assets/boot/happy-zac.png`) is the boot logo; the Welcome box shows `assets/boot/zacos-logo.png` (original assets, not the Apple logo). The 1984 Macintosh screenshot in `upload assets/` is a layout reference only: never ship or commit it.
- `macos/` and `uploads/` are for measurement reference only; nothing from them ships.
- Marks must not infringe Apple trademarks.

## Network / AFP notes

- Mac OS 9 AFP server: AFP 1.1–2.1, Cleartxt / Randnum / 2-Way Randnum auth.
- Classic forwards Mac port 548 → localhost:5548 (`--redir tcp:5548::548`).
- **Critical:** send each DSI message in a single `sendmsg()` with iovec. Mac OS 9's server misreads split messages.
- Netatalk can't nest volumes: `[Homes]` must veto shared sub-folders (handled in `sharing/zacos9-sharing-helper`).
- Debug tracing: `ZACOS9_AFP_DEBUG=1 build/network/zacos9-afp mount ...`
- **Never use `QProcess::waitForFinished()` in this codebase.** It deadlocked live in a Qt/Wayland GUI process even when the child process had already exited cleanly (confirmed from the server's own log). Use the `processEvents()`-loop pattern (`runSharingHelper`, `runAppstoreHelper`, `waitForProcess` in `shell/network/afpclient.cpp`) everywhere instead.
- `zacos9-afp`'s own argument parser doesn't recognize `--` as an end-of-options marker — it reads it as an unknown flag and refuses. Don't add one when building its command line.
- `avahi-browse` takes at most one service type per invocation (two args = "Too many arguments", browses nothing); use `-a` and filter client-side. Also needs `-k` or it substitutes a friendly name for the type even with `-p`.
- `gio mount`'s exit code is 0 whether or not the mount worked; go by stderr.

## Agent / subagent policy

- Spawn `Explore` for broad symbol searches spanning >3 directories.
- Spawn a background agent only for genuinely independent parallel work.
- Do not spawn just to avoid reading a file yourself.

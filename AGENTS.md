# AGENTS.md — ZacOS 9

A Platinum-style Wayland desktop for Debian: C/wlroots compositor, Qt6 shell,
packaged as `zacos9`. Build and test inside Debian 13 (WSL `Debian` on the dev
box). `CLAUDE.md` has the full component map and is loaded alongside this file;
keep both in sync.

## Commands

```sh
scripts/build.sh                    # meson setup build on first run, then ninja
scripts/build.sh compositor/zacos9-wm   # args go to ninja: build one target
meson test -C build --list          # 150+ registered test names
meson test -C build --no-rebuild --print-errorlogs <name>
meson test -C build --num-processes 1   # whole suite: tests share gvfs, D-Bus, compositor, apt
scripts/build-debs.sh               # package; this is the full serial test gate
scripts/install-live.sh [--no-build]    # install on this machine; log out/in afterwards
scripts/dev-boot.sh                 # boot local tree in QEMU (needs an earlier ISO build)
scripts/run-nested.sh               # build and run zacos9-wm inside the current session
```

- Tests live in each component's `meson.build`; a new test file does nothing
  until it is registered there. Names often differ from filenames — use
  `--list` rather than guessing (`finder-vfs` is `shell/finder/tests/test_vfs.cpp`).
- `debian/rules` runs `dh_auto_test --no-parallel`. Do not pass `nocheck` or a
  `nocheck` profile; a successful package build does not mean every test ran.
  The CI gate allows only these skips: `menudraw`, `widgets`, `decor`,
  `screen-sharing-session`, `afp-disconnect`.
- `menudraw`/`widgets`/`decor` skip when `tools/measure/figs` is absent (it is
  gitignored Apple reference material — never commit it).
- Local `build-debs.sh` packages the **working tree** (uncommitted edits and
  new untracked files, minus `.gitignore`); CI packages `git archive HEAD`.
- Do **not** rebuild the ISO to test a change; use `dev-boot.sh`. It requires
  `scripts/build-iso.sh` to have run once (`/var/tmp/zacos9-iso/chroot`).
  `--record` saves frames to `/tmp/devboot`; serial login:
  `python3 tests/vm/serial.py ~/.local/share/zacos9-vm/serial.sock '<cmd>'`.
- UI tests start a nested compositor and are visual, not pass/fail:
  `scripts/snapshot.sh /tmp/out.png <delay> 'sh tests/ui/finder-drag.sh'`.
  The script sets `WLR_RENDERER=pixman`, needs `grim`, times out after 60 s,
  and drives `build/shell/vptr`; inspect the PNGs it writes. Each UI script's
  header says what to set up and which screenshots to expect.

## Gotchas

- **Never use `QProcess::waitForFinished()` to await normal completion** — it
  deadlocks in this Qt/Wayland session. Use the `processEvents()` loop in
  `shell/network/afpclient.cpp` (`runSharingHelper`, `runAppstoreHelper`).
- AFP: send each DSI message in one `sendmsg()` with iovec (split messages are
  misread by Mac OS 9). `zacos9-afp` has no `--` end-of-options marker.
- Network discovery launches one `avahi-browse` per service type with `-k -p -r
  -t`; `-a` is too slow for the scan budget. Prefer the existing pattern in
  `shell/network/discovery.cpp`.
- `gio mount` exits 0 even when mounting failed — check stderr.
- In packages, untracked files ship and `.gitignore`d files never do.
  `macos/`, `uploads/`, `upload assets/`, ROMs and reference PDFs must never be
  committed or packaged.
- Generated assets: after changing icon masters run `tools/icons/build_icons.py`;
  boot-art masters use `tools/boot/make-boot-art.py`. `setos.efi` is built by
  `hardware/intel-graphics/build-setos.sh` (gnu-efi) and is gitignored.
- `PROJECT_STATUS.md` is ~2,000 lines; grep it for the relevant feature instead
  of reading it whole. Subsystem details live in `docs/*.md`.
- User-facing name is **Sniffer**; internal names (`zacos9-finder`, D-Bus
  interfaces, `shell/finder/`) stay for compatibility.
- No Apple assets or trademarks: use Happy Zac (`assets/boot/happy-zac.png`) and
  the original ZacOS logo.

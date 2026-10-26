# Contributing to ZacOS 9

Thanks for helping. This page gets you from a fresh clone to a running desktop
and a passing test, then explains how changes are made here. For how the
pieces fit together, read [ARCHITECTURE.md](ARCHITECTURE.md); each part also
has its own page in [`docs/`](docs/).

New here? Issues labelled
[good first issue](https://github.com/chromasystemprofessional/Zac-OS-9/labels/good%20first%20issue)
are small, self-contained and say which files and tests are involved.

## What you need

- **Debian 13 (trixie)**, on a real machine, in a VM, or in WSL 2 on Windows.
  ZacOS 9 builds against Debian 13's wlroots 0.18 and Qt 6.8. Other
  distributions have different versions and aren't supported for building.
- About 5 GB of disk for the build, more for packages and ISOs.

## Set up

```sh
sudo apt install git build-essential devscripts
git clone https://github.com/chromasystemprofessional/Zac-OS-9.git
cd Zac-OS-9
sudo apt build-dep ./      # everything debian/control lists to build and test
```

`apt build-dep ./` reads the dependencies from `debian/control`, so it stays
right as they change. CI starts from a clean Debian 13 and runs these same
steps on every pull request.

### On Windows (WSL 2)

Install the `Debian` distribution (`wsl --install -d Debian`) and upgrade it
to Debian 13 if it's older, then follow the steps above inside it. WSLg shows
ZacOS 9's window on your Windows desktop.

- **`run.cmd`** in the project folder builds and runs the desktop in a window;
  `vm.cmd` runs an ISO in a virtual machine. Both work from Explorer or a
  PowerShell terminal opened in the folder (`.\run`, `.\vm`).
- The window often opens behind others: look for "wlroots - WL-1 (Debian)" on
  the taskbar.
- If the window shows on the taskbar but draws nothing, WSLg's display bridge
  has gone stale (common after sleep or monitor changes): run `wsl --shutdown`
  in PowerShell and start again.

## Build

```sh
scripts/build.sh                        # sets up build/ the first time, then builds
scripts/build.sh compositor/zacos9-wm   # one target (arguments go to ninja)
```

The first build takes a few minutes. `build/` is yours; nothing in it is
committed.

## Run it

**In a window**, inside the desktop you're already using (any Wayland or X11
session):

```sh
scripts/run-nested.sh                   # opens a terminal inside ZacOS 9
scripts/run-nested.sh -s 'xterm'        # starts something else instead
scripts/run-nested.sh -S 2              # at 2x, for a 4K screen
```

The window is a whole ZacOS 9 desktop: menu bar, Sniffer, windows. Quit with
**Ctrl+Alt+Backspace** inside it, or close the window. This is the quickest
way to try a change to the compositor or a program.

**In a virtual machine**, for what only a whole system shows (start-up, the
login window, the installer, sound):

```sh
scripts/build-iso.sh     # once: builds an ISO (about 35 minutes, asks for sudo)
scripts/dev-boot.sh      # then: your tree in a VM in a few minutes
```

`dev-boot.sh` puts your build on top of the last ISO's system, so you don't
rebuild the ISO for each change. Its options (`--disk`, `--uefi`, `--record`)
are at the top of the script; [Packaging](docs/packaging.md) has the rest.

**On your own computer**, if it runs ZacOS 9: `scripts/install-live.sh` builds
the package and installs it (one sudo prompt); log out and in to use it.

`scripts/build-debs.sh` builds the packages from your working tree, so
uncommitted changes and new untracked files go in (`.gitignore`d ones don't);
CI builds from the commit. Building the package runs the whole test suite.

## Test

```sh
meson test -C build --list                              # every test's name
meson test -C build disksetup-plan --print-errorlogs    # one test
meson test -C build $(meson test -C build --list | grep disksetup)   # those named for one part
meson test -C build --num-processes 1                   # all of them
```

- Test names don't always match file names; `--list` shows them, and each
  part's `meson.build` registers its tests.
- Run the whole suite one test at a time (`--num-processes 1`): some tests
  share D-Bus, the file system's mount service, or apt.
- **Tests never draw on your desktop.** Tests that need a screen start their
  own headless compositor, on a private D-Bus and in a temporary home folder.
  `tests/disksetup/stage.py` does this for you if you're writing one.
- A new test file does nothing until it's registered in a `meson.build`.
- Five tests may skip, and only these: `menudraw`, `widgets` and `decor`
  compare against Apple reference figures that are never committed
  (`tools/measure/figs/`), and `screen-sharing-session` and `afp-disconnect`
  need a live PipeWire session and an AFP server. CI fails if any other test
  skips.
- `tests/ui/*.sh` are visual checks, not pass or fail: run one with
  `scripts/snapshot.sh /tmp/out.png 3 'sh tests/ui/finder-drag.sh'` and look
  at the screenshots; each script's header says what to expect.

Two checks look after the build itself (see
[`tests/baseline/`](tests/baseline/README.md)):

- **`installed-files`** fails if a file stops being installed, or a new one
  appears that the list doesn't have. If the change is meant, update the list
  with the command the test prints, and the change shows in your pull request.
- **CI counts compiler warnings** and fails if a change adds one.

## Making a change

1. **Start from an issue**, or open one first for anything large, so the
   approach can be agreed before you write it.
2. **Branch** from `main`. One change per pull request: a fix, a feature, or a
   cleanup, not several.
3. **Test it**: add or extend a test that fails without your change, where
   that's possible, and run the tests near what you touched.
4. **Update the docs** your change makes wrong: the part's page in `docs/`,
   and ZacOS Help (`share/zacos9/help/`) if it changes what people see.
5. **Open the pull request** with what changed, why, and how you checked it,
   including what you couldn't check (real hardware, say). The template asks
   for these.

CI builds the Debian package from your branch and runs every test (about 40
minutes). A pull request merges once CI passes and it's been reviewed.

## Code style

Match the code around you; there's no reformatting tool and no reformatting
sweep.

- **C** (compositor, menu bar, lock, `lib/`): C11, tabs, `snake_case`, braces
  on the same line.
- **C++** (most programs under `shell/`): C++17, Qt 6, tabs, `camelCase`,
  members `m_name`.
- **Shell**: POSIX `sh`, `set -eu`, quoted variables.
- **Comments say why**, not what: the reason, the bug it avoids, the Mac OS
  behaviour it copies.
- Names people see follow Mac OS 9's wording ("Sniffer", "Control Panels",
  "Get Info"); internal names (`zacos9-finder`, D-Bus names) stay as they are
  for compatibility.

## Rules learned the hard way

- **Never wait for a process with `QProcess::waitForFinished()`.** It has
  deadlocked in a Qt/Wayland program even after the child had exited. Use a
  `processEvents()` loop (`waitForProcess` in `shell/network/afpclient.cpp`)
  or the `finished` signal.
- **AFP: send each DSI message in one `sendmsg()`** with an iovec. Mac OS 9's
  server misreads a message split across calls.
- **`gio mount` exits 0 even when mounting failed**: check its error output.
- **`avahi-browse` takes one service type per run** (a second one makes it
  browse nothing): start one per type (`-k -p -r -t`), in parallel. Not `-a`:
  it's too slow for the scan, and can leave only unusable records.
- **`zacos9-afp` has no `--` end-of-options marker**: it refuses `--` as an
  unknown option, so don't add one to its command line.
- **Nothing from Apple ships**: no code, ROMs, fonts, icons, sounds or
  trademarks used as ours (no Happy Mac, no startup chime). `macos/`,
  `uploads/`, `upload assets/` and `tools/measure/figs/` are reference
  material for measuring against, never committed or packaged. The boot logo
  is Happy Zac (`assets/boot/happy-zac.png`) and the Welcome box shows the
  ZacOS logo (`assets/boot/zacos-logo.png`), both original.
- **Generated files**: after changing icon masters run
  `tools/icons/build_icons.py`; boot art comes from `tools/boot/make-boot-art.py`.
- **`emulation/SheepShaver/`** is upstream code: change it upstream, not here.

## Where things are

[ARCHITECTURE.md](ARCHITECTURE.md) has the map: the processes, how they talk,
and which folder to change for what. [`docs/`](docs/) has a page per part
(the installer, networking, Screen Sharing, the Memory panel…), and
[PROJECT_STATUS.md](PROJECT_STATUS.md) says what's being worked on now.

## Licence

ZacOS 9 is GPL-3.0-or-later. By sending a pull request you agree to license
your contribution under it.

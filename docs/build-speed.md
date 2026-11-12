# Build speed

The release package build is deliberately thorough: it runs every test, and
`debian/rules` runs them one at a time. The local build can be much quicker.

## What the build was doing

A full debug build had **1,110 compile commands for 450 source files**: 660
of them were recompiling files another target had already compiled. Most of
that was the Finder test suite, which compiled the Finder's own sources once
per test executable (15–16 copies), and `storeclient.cpp`, which was compiled
19 times. Of about 4,177 seconds of compiler CPU time in one build, roughly
2,767 seconds were duplicate copies.

## What changed

- `shell/finder/meson.build` builds `finder_test_sources` once as
  `finder_test_lib`; the Finder tests link it instead of recompiling it.
- `shell/store/meson.build` builds `storeclient_lib`, used by the Finder, the
  Store, Software Update and their tests.
- `shell/collar/meson.build` builds `collar_lib`; the Collar and its UI tests
  link it.
- `shell/panels/meson.build` builds `soundclient_lib`, shared by the Sound
  panel, the Collar and their tests.
- Test executables are `build_by_default: false`. A plain `ninja` builds the
  applications, not all 146 test programs; `meson test` builds the tests it
  runs.

The result is **703 compile commands for 451 sources** (252 duplicates) and
about **1,016 Ninja edges in the whole build**, down from 1,386. A default
`ninja` now builds the applications only and skips the test binaries; on the
development machine (6 cores/12 threads) a clean default build fell from
**383 s to 185 s**. `meson test` builds the tests it runs. The remaining
duplicates are mostly generated Wayland protocol files and small shared
helpers; they are optional further work.

## The fast loop

```sh
sudo apt install ccache mold   # optional but recommended
scripts/build-fast.sh          # -O2, no debug info, ccache/mold if present
scripts/build-fast.sh shell/finder/zacos9-finder shell/menubar/zacos9-menubar
meson test -C build-fast finder-selection
```

`build-fast/` is separate from `build/`; the debugging build is untouched.
`ZACOS9_FAST_BUILD_DIR` names a different directory. The script uses
`-Doptimization=2 -Ddebug=false`, `-pipe`, ccache when installed, and
mold or lld when installed. Without those tools it still builds normally.

For a single change, build only the targets involved:

```sh
ninja -C build zacos9-finder zacos9-controlpanel
# or
scripts/build.sh zacos9-finder
```

`meson test -C build --num-processes N` is fine for a subset of local tests;
the whole suite shares D-Bus, mount and compositor state, so use
`--num-processes 1` for it, as the package build does. The package test phase
will not get faster with more cores; faster single-thread performance and
fast storage are what help it.

## Measuring

```sh
time ninja -C build
time meson test -C build --num-processes 1
time scripts/build-debs.sh        # includes the package's serial tests
printf 'compile commands: '; python3 -c \
  'import json; print(len(json.load(open("build/compile_commands.json"))))'
```

`build/build.ninja` records how many edges are left; `.ninja_log` has the
duration of each compile. `ccache -s` shows the hit rate once ccache is in
use.

## Still optional

- Shared static libraries for the generated Wayland protocol C files
  (compiled 8–11 times each).
- A `kblayout` library for `compositor/src/kblayout.c` and `xkblayouts.cpp`.
- Precompiled headers for the Qt-heavy Finder/panel targets.
- Unity builds for the many small C files, after checking for name clashes.

Hardware advice is in the same spirit: **single-core speed and fast NVMe
matter more than a very high core count**, because the package tests are
serial. A modern 8–12 core CPU, 32–64 GB of RAM and a 2 TB NVMe are a good
fit; a 32-core server chip is not.

# After Dark modules

ZacOS can run the original 68k *After Dark* screen savers, the ones Berkeley
Systems sold from 1989 to 1997 (Flying Toasters, Starry Night, Satori and the
rest). They are not built-in: you supply an After Dark disk image or a folder
of modules, import it, and choose a module in the Screen Saver control panel.

The modules and the Macintosh fonts they draw with are Berkeley Systems' and
Apple's. **No modules, fonts or disk images are in the repository or the
package.** The import copies them out of the user's own disk into
`$XDG_DATA_HOME/after-dark/modules` (normally
`~/.local/share/after-dark/modules`), the same library the standalone
[After Dark Rust player](https://github.com/panth-net/after-dark-repository-rust)
uses.

## Importing

1. Open **Zac menu ▸ Control Panels ▸ Screen Saver**.
2. Choose **After Dark** in the saver list, or click a module in the
   **After Dark Modules** list.
3. Click **Import…** and choose a disk image, or drop a disk image or a
   folder of modules onto the panel.
4. Choose **After Dark** and the module to use.

Supported sources are HFS disk images (`.img`, `.dsk`, `.iso`, `.toast`,
`.cdr`, `.image`, or a bare dump; the volume is found by its signature), a
folder of already-extracted `.rsrc` files, one `.rsrc` in such a folder, or a
compressed archive (`.zip`, `.sit`, `.sitx`, `.sea`, `.cpt`, `.hqx`) that
`unar` can unpack. An import that finds modules but no font is refused: the
modules came with the System file, and the runtime needs its fonts for text.

The import replaces the library in one step: a failure halfway through leaves
the previous library untouched, and importing a second disk replaces the
first library rather than adding to it.

## How it runs

`emulation/afterdark/` vendors the MIT-licensed Rust engine from
`panth-net/after-dark-repository-rust` (commit
`8625c19e342f47e53fddc05ee41aa355921a7e22`), with the windowed player and its
audio/display dependencies removed and a headless helper added. The helper:

- loads the module's `ADgm` code resource and the fonts beside it;
- runs its `Initialize` / `Blank` / `DrawFrame` lifecycle on the emulated 68k
  machine (Musashi) and the reimplemented Mac Toolbox;
- writes 640x480 indexed frames to a pipe, 30 times a second, as
  `ADF1` records (header, 256-colour palette, palette indices);
- exits when `zacos9-screensaver` stops reading or kills it.

`zacos9-screensaver` owns one helper per screen, reads the latest whole frame
without blocking its Wayland loop, and scales it into the existing overlay
with black bars around 4:3. The Screen Saver panel runs a second helper for
its 130x100 live preview. A helper that never produces a frame is stopped
after five seconds and the built-in saver covers the screen instead.

The module list is filtered by the engine's compatibility survey: modules that
need the After Dark 3.0 engine, or that the survey found do not draw, are not
offered. Games that expect input (Lunatic Fringe, for example) show their
attract screen; the screen saver takes no input, like every other saver. There
is no sound. Each screen runs its own emulator, so the saver uses one CPU core
per screen showing a module.

## Settings

| Key | Default | What it does |
|---|---|---|
| `screensaver` | `patterns` | `afterdark` selects the imported module |
| `screensaver-afterdark` | unset | the module's filename without `.rsrc` |

Both are in `~/.config/zacos9/desktop.conf`, like the other screen saver
settings. The rest of the idle rules are unchanged; see
[Energy Saver and Screen Saver](energy-saver.md).

## Building

The helper is a Rust binary. Debian's `cargo` and `rustc` (1.85 or newer) are
build dependencies; the vendored workspace has no crates.io dependencies, so
the build is offline. Meson's `-Dafterdark=auto` (the default) builds it when
`cargo` is found and skips it otherwise; `-Dafterdark=enabled` makes a missing
cargo an error, and `-Dafterdark=disabled` leaves it out even when cargo is
present. A build without it still has the four built-in savers and the panel
says After Dark is not installed.

```sh
# Just the helper, without the rest of ZacOS:
emulation/afterdark/build.sh /tmp/zacos9-afterdark /tmp/afterdark-target
```

Afterglow, the macOS app that also runs After Dark modules, is not used here:
it is a closed-source macOS 14+ application. The Rust engine is the
open-source equivalent for Linux and exposes the frame callback this saver
needs.

## Checking it

- `afterdark-frame` (C) runs a fake helper and checks the frame records,
  palette, delayed swap and letterbox scaling.
- The vendored Rust workspace's own tests: `cargo test --workspace --offline`.
- Real modules are copyrighted and not in CI; playback is checked by importing
  a user's disk image and choosing a module in the panel.

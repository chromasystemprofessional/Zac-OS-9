# After Dark modules for the ZacOS screen saver

This folder vendors the MIT-licensed After Dark engine from
[`panth-net/after-dark-repository-rust`](https://github.com/panth-net/after-dark-repository-rust),
commit `8625c19e342f47e53fddc05ee41aa355921a7e22` (2026-08-01), and adds a
headless `zacos9-afterdark` helper. It runs the original 68k After Dark module
code (Musashi, the Mac Toolbox reimplementation) and writes frames to stdout;
`zacos9-screensaver` scales them into its overlay.

**No original modules, fonts or disk images are in this tree.** The import
reads a disk the user supplies and extracts its resource forks to
`$XDG_DATA_HOME/after-dark/modules` (normally
`~/.local/share/after-dark/modules`). The modules and fonts remain Berkeley
Systems' and Apple's.

## What was changed from upstream

- `ad-player` (the windowed minifb player) and its display/audio dependencies
  are not vendored. The ZacOS saver is headless and silent.
- `ad-runtime`'s `device` module and its optional `cpal` dependency are
  removed for the same reason.
- `ad-m68k/build.rs` invokes `cc` and `ar` directly instead of the `cc` crate,
  so the workspace has no crates.io dependencies and builds offline with
  Debian's `rustc`/`cargo`. The upstream `AD_M68K_REGENERATE` path is not
  included; the committed opcode tables are used as they are.
- `crates/ad-saver` is new: `zacos9-afterdark` (import, list, headless render).

## Build

The ZacOS build runs this through `emulation/afterdark/build.sh` when `cargo`
is available. To build it by hand:

```sh
CARGO_TARGET_DIR=/tmp/afterdark-target \
  cargo build --release --locked --offline \
  --manifest-path emulation/afterdark/Cargo.toml -p ad-saver
```

The binary is `$CARGO_TARGET_DIR/release/zacos9-afterdark`.

## Licence

The Rust code is MIT; see [`LICENSE`](LICENSE). The vendored Musashi core has
its own notice in [`LICENSE-musashi.txt`](LICENSE-musashi.txt) and under
`crates/ad-m68k/vendor/`. The After Dark name and modules belong to their
respective rights holders; this project is not affiliated with or endorsed by
Berkeley Systems or the current rights holders.

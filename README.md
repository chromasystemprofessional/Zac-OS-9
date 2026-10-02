# Platinum 2026

A strict, pixel-faithful replica of the Mac OS 8/9 "Platinum" desktop, built as a
desktop environment for Debian Linux, with built-in support for running classic
Mac software through emulation.

No Apple code, ROMs, fonts, icons or sounds are included. Every asset is original
or freely licensed. To run classic Mac apps, you supply your own ROM and system
software.

## Layout

| Path | What |
|---|---|
| `compositor/` | `platinum-wm`: Wayland compositor (C, wlroots 0.18). Draws window frames and handles input. |
| `shell/` | Menu bar, Finder, Control Panels (Qt6). *Phase 2+* |
| `style/` | Platinum QStyle plugin and GTK theme. *Phase 5* |
| `emulation/` | SheepShaver / Basilisk II integration. *Phase 4* |
| `assets/` | Original fonts, icons, desktop patterns, sounds. |
| `docs/` | Roadmap, fidelity references, design notes. |
| `scripts/` | Build and run helpers; package, ISO and VM scripts. |
| `session/` | The login session: `platinum-session` and its `wayland-sessions` entry. |
| `debian/`, `packaging/`, `iso/` | Debian packaging, the emulator package, the live-build configuration. |

## Developing on Windows (WSL2 + WSLg)

The source tree lives on the Windows drive. Everything builds and runs inside the
`Debian` WSL distro, and WSLg shows the compositor as a normal window.

To run it, double-click **`run.cmd`** in the project folder, or type `.\run` in a
VS Code / PowerShell terminal opened in this folder.

This builds the code and opens `platinum-wm` with a terminal (`foot`) running
inside it. **The window often opens behind other windows.** Look for
"wlroots - WL-1 (Debian)" on the taskbar. To quit, press **Ctrl+Alt+Backspace**
inside it or close the window.

Options:
- `-d` turns on debug logging.
- `-S 2` scales everything 2× (crisp on 4K screens).
- `-s CMD` runs CMD at startup.

For example: `.\run -S 2 -s "foot & xterm"`.

Both Wayland and X11 apps work. Under WSLg the run scripts give the compositor
a private `/tmp/.X11-unix` (see `scripts/x11-namespace.sh`), because WSLg's copy
is read-only.

**If the window shows on the taskbar but draws nothing** (you see your desktop
wallpaper through it), WSLg's display bridge has gone stale. This is common
after sleep or monitor changes on multi-monitor setups. Run `wsl --shutdown` in
PowerShell and launch again. `wsl --terminate Debian` is not enough, because it
doesn't restart WSLg.

Build only: `wsl -d Debian -- scripts/build.sh`

## Packages, ISO and VM

- `scripts/build-debs.sh` builds the `.deb` packages.
- `scripts/build-iso.sh` builds a live and install ISO that boots straight into
  Platinum 2026.
- **`vm.cmd`** runs that ISO in a QEMU virtual machine, with a real boot, login
  and screen.
- `scripts/dev-boot.sh` tries system and boot changes in a VM in a few minutes,
  without building the ISO.

See [docs/packaging.md](docs/packaging.md).

## License

GPL-3.0-or-later.

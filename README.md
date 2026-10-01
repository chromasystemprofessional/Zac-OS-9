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
| `scripts/` | Build and run helpers. |

## Developing on Windows (WSL2 + WSLg)

The source tree lives on the Windows drive. Everything builds and runs inside the
`Debian` WSL distro, and WSLg shows the compositor as a normal window.

```sh
# from PowerShell, in the project folder:
wsl -d Debian -- scripts/run-nested.sh
```

This builds the code and opens `platinum-wm` with a terminal (`foot`) running
inside it. To quit, press **Ctrl+Alt+Backspace** or close the window.
Until title bars exist (Phase 1), move a window by holding **Alt** and dragging it.

Build only: `wsl -d Debian -- scripts/build.sh`

Full-session testing (real login, boot) happens in a Debian VM; see `docs/ROADMAP.md`.

## License

GPL-3.0-or-later.

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

To run it, double-click **`run.cmd`** in the project folder, or type `.\run` in a
VS Code / PowerShell terminal opened in this folder.

This builds the code and opens `platinum-wm` with a terminal (`foot`) running
inside it. **The window often opens behind other windows.** Look for
"wlroots - WL-1 (Debian)" on the taskbar. To quit, press **Ctrl+Alt+Backspace**
inside it or close the window.

Add `-d` for debug logging: `.\run -d -s foot`.

**If the window shows on the taskbar but draws nothing** (you see your desktop
wallpaper through it), WSLg's display bridge has gone stale. This is common
after sleep or monitor changes on multi-monitor setups. Run `wsl --shutdown` in
PowerShell and launch again. `wsl --terminate Debian` is not enough, because it
doesn't restart WSLg.

Build only: `wsl -d Debian -- scripts/build.sh`

Full-session testing (real login, boot) happens in a Debian VM; see `docs/ROADMAP.md`.

## License

GPL-3.0-or-later.

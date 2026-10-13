# Copilot Instructions for ZacOS 9

## Build, Test, and Lint

### Build Commands
- **Standard build**: `scripts/build.sh`
- **Package and install locally**: `scripts/install-live.sh` (Use `--no-build` to install without rebuilding)
- **Interactive VM boot**: `scripts/dev-boot.sh` (Use this for rapid iteration; avoid rebuilding the ISO)

### Testing
- **AFP Client (Netatalk)**: `AFP_PASSWORD=Sesame-42 tests/sharing/afp-client.sh 127.0.0.1 "platest's home" platest`
- **AFP Client (Mac OS 9 via Classic)**: `AFP_PASSWORD=macpass tests/sharing/afp-client.sh 127.0.0.1:5548 "Macintosh HD" "^Mac User"`

## Architecture Overview

ZacOS 9 is a reconstructed desktop environment aiming for a classic Macintosh feel on Linux.

| Component | Path | Tech Stack |
|---|---|---|
| **Compositor** | `compositor/` | C / wlroots 0.18 |
| **Finder** | `shell/finder/` | C++ / Qt6 (VFS via GIO) |
| **Menu Bar** | `shell/menubar/` | C++ / Qt6 |
| **Control Panels** | `shell/panels/` | C++ / Qt6 |
| **App Store** | `shell/store/` | C++ / Qt6 (apt wrapper) |
| **Software Update** | `shell/update/` | C++ / Qt6 |
| **Network Browser** | `shell/network/` | C++ / Qt6 |
| **AFP Client** | `network/afp/` | C / libfuse3 |
| **Boot Process** | `boot/` | Plymouth / C / shell |

## Key Conventions & Constraints

### Development Rules
- **Qt/Wayland Concurrency**: **NEVER** use `QProcess::waitForFinished()`. It causes deadlocks in this environment. Use the `processEvents()`-loop pattern (see `shell/network/afpclient.cpp`).
- **Networking**: When implementing AFP, send each DSI message in a single `sendmsg()` using `iovec` to avoid server-side misreads.
- **Builds**: Redirect long build output to `build/build.log` and use `tail` to inspect failures.

### Legal & Brand Constraints
- **No Apple Assets**: Absolutely no official Apple logos, fonts, icons, or the startup chime.
- **Custom Branding**: Use "Happy Zac" (`assets/boot/happy-zac.png`) and the ZacOS logo.
- **Reference Only**: Files in `macos/` and `uploads/` are for measurement only; they must never be shipped or committed.

### Tooling Notes
- **AFP Debugging**: Use `ZACOS9_AFP_DEBUG=1 build/network/zacos9-afp mount ...`
- **Avahi**: `avahi-browse` only accepts one service type per invocation; use `-a` and filter results client-side.
- **GIO**: `gio mount` returns exit code 0 even on failure; always verify via stderr.

# AGENTS.md — ZacOS 9

A Platinum-style desktop for Debian 13: a C/wlroots compositor, a Qt 6 shell,
packaged as `zacos9`.

**Read these first; they are the source of truth for people and agents:**

- [CONTRIBUTING.md](CONTRIBUTING.md): setting up, building, running, testing,
  code style, and the rules learned the hard way (never
  `QProcess::waitForFinished()`, one `sendmsg()` per AFP DSI message, nothing
  from Apple ships…).
- [ARCHITECTURE.md](ARCHITECTURE.md): the processes, how they talk, and which
  folder to change for what.
- `docs/<part>.md`: each part in detail. [PROJECT_STATUS.md](PROJECT_STATUS.md):
  what's being worked on.

## Particular to agents

- Never read or edit `build/`, `emulation/SheepShaver/` (upstream),
  `uploads/`, `upload assets/`, `macos/` or `tools/measure/figs/` (Apple
  reference material that must never ship).
- One logical change per pull request; no unrelated cleanup.
- Never rebuild the ISO to test a change: use `scripts/dev-boot.sh`.
- Never run a nested or headless desktop on the user's own session bus or
  home folder; `tests/disksetup/stage.py` shows how to isolate one.
- PROJECT_STATUS.md is the current state, not a log: update it when the state
  changes, and add no "what I did" rows.

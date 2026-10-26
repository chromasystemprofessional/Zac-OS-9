# Copilot instructions for ZacOS 9

The project's docs are the source of truth; follow them rather than anything
remembered:

- [CONTRIBUTING.md](../CONTRIBUTING.md): building, running, testing, code
  style, and the hard rules (never `QProcess::waitForFinished()`, one
  `sendmsg()` per AFP DSI message, nothing from Apple ships…).
- [ARCHITECTURE.md](../ARCHITECTURE.md): the processes, how they talk, and
  where to change what.
- `docs/<part>.md`: each part in detail.

Never read, suggest from, or commit `macos/`, `uploads/`, `upload assets/` or
`tools/measure/figs/`: they're Apple reference material that must never ship.

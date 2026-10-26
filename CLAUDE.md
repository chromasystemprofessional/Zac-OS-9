# CLAUDE.md — ZacOS 9

The project's own docs are the source of truth, for people and agents alike:
building, running, testing, code style and the hard rules are in
CONTRIBUTING.md; the map of the code is in ARCHITECTURE.md; each part has a
page in `docs/`. Both are imported here:

@CONTRIBUTING.md
@ARCHITECTURE.md

What's below is only what's particular to working as an agent.

## Never read or edit

`build/`, `emulation/SheepShaver/` (upstream), `uploads/`, `upload assets/`,
`macos/` and `tools/measure/figs/` (Apple reference material: never ship it).

## Workflow rules

1. Find the smallest set of relevant files before reading: ARCHITECTURE.md's
   "Where do I change…" table, then the part's page in `docs/`.
2. Read only the needed sections of large files (`offset`/`limit`).
3. Use `rg` for symbol searches, excluding the folders above.
4. Redirect long build output: `ninja -C build >build/build.log 2>&1; tail -20 build/build.log`.
5. One logical change per session and per pull request. No unrelated cleanup.
6. For changes touching more than 3 files, write a short plan first.
7. Run focused tests before broader ones; the whole suite one at a time.
8. When a task changes what's true about the project (something finished,
   started, put on hold or found missing), update PROJECT_STATUS.md. It's the
   current state, not a log: no "what I did" rows.
9. **Never rebuild the ISO to test a change**: use `scripts/dev-boot.sh`. The
   ISO is built only when asked for.
10. Never run a nested or headless desktop on the user's own session bus or
    home folder (tests/disksetup/stage.py shows how to isolate one).

## Agent / subagent policy

- Spawn `Explore` for broad symbol searches spanning more than 3 directories.
- Spawn a background agent only for genuinely independent parallel work.
- Do not spawn just to avoid reading a file yourself.

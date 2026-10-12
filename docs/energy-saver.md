# Energy Saver and Screen Saver

`zacos9-wm` decides when screens and the system go to sleep
(`compositor/src/idle.c`, with the timing rules in `idle_policy.c`), and
starts the screen saver (`zacos9-screensaver`, `shell/screensaver/`) on
the screens whose turn it is. Two control panels set it up, in the Apple
menu's Control Panels and the Finder's Control Panels folder:

- **Energy Saver** (`zacos9-controlpanel energysaver`): when the display
  and the system sleep.
- **Screen Saver** (`zacos9-controlpanel screensaver`): which screen
  saver, with a live preview, and when it starts.

## Settings

In `~/.config/zacos9/desktop.conf`, times in minutes (0 meaning never).
The compositor applies changes as soon as the file changes, like the other
panels' settings.

| Key | Default | What happens |
|---|---|---|
| `screensaver` | `patterns` | which screen saver; `none` for none |
| `screensaver-after` | 5 | the screen saver starts |
| `display-sleep` | 10 | the display goes dark |
| `system-sleep` | 0 (never) | `systemctl suspend` |
| `require-password` | unset (off) | `1`: the screens lock, and only your password wakes them |

The Energy Saver sliders stop at 1, 2, 3, 5, 10, 15, 20, 30, 45, 60, 90
and 120 minutes, and Never; the Screen Saver's at 1 to 60 minutes (None is
its own choice). System sleep is off by default: suspend and resume isn't
proven on every machine ZacOS runs on (Macs especially).

## The screen savers

Original pixel art (`lib/savers.c`, shared by `zacos9-screensaver` and the
panel's preview), drawn at the screen's logical size and scaled up
nearest-neighbour, 25 frames a second:

| Id | Name | |
|---|---|---|
| `patterns` | Desktop Patterns | ZacOS's desktop patterns fill the screen in 32-pixel blocks, one by one in no order; every 12 s the next pattern takes over the same way |
| `stars` | Starfield | stars coming towards you, nearer ones brighter |
| `logo` | Bouncing Logo | the ZacOS logo bouncing off the edges (double size on a screen 512 pixels wide or more) |
| `blank` | Blank Screen | black |

`zacos9-screensaver` is started when a screen's turn begins. It covers
each screen in the `saver` state with an overlay layer surface (namespace
`screensaver`, above full-screen windows and the menu bar), hides the
pointer over it, follows the state file as screens change, and exits once
no screen is in its turn. The compositor hides its surfaces the moment
their screen wakes, without waiting for it, and reaps it when it exits.
A click or key that wakes the screens doesn't reach anything (see below),
so the screen saver itself never takes input.

## Rules

- Every screen is idle for as long as there has been no input (keys, the
  mouse, the scroll wheel). Each screen then shows the screen saver until
  it goes dark. A screen saver timed later than display sleep never shows.
- **A screen holding a full-screen window stays awake**, and so does one
  showing a surface with an idle inhibitor (the Wayland
  `idle-inhibit-unstable-v1` protocol: Firefox, mpv and others while
  playing video). The other screens sleep as usual: one screen can play a
  film while the other shows the screen saver, then goes dark.
- The system sleeps only when no screen is being kept awake. After it
  wakes, its timer starts again.
- Any input wakes every screen. A click on a dark screen, or a key while
  every screen is dark, only wakes them: it doesn't reach the window
  underneath, so nobody clicks or types into what they can't see. Its
  release is swallowed too.
- Plugging in a screen wakes the others.
- Only real screens (DRM) are turned off, and only a real session
  suspends. Nested (`dev-boot.sh` in a window) or headless, the states are
  only reported, never the host's own screens or sleep.

## Asking for a password to wake

**Ask for my password to wake**, a checkbox in the Screen Saver panel
(`require-password=1`), is off unless you turn it on. With it on, the
session locks (`compositor/src/lock.c`, with `ext-session-lock-v1`):

- once **every** screen shows the screen saver or has gone to sleep — not
  while a full-screen window (or an app's request) keeps a screen awake:
  someone is watching;
- **before the system sleeps**: idle system sleep waits until the lock is
  in place, and `zacos9-inhibit` holds a logind "delay" inhibitor, so
  Special > Sleep and closing a laptop's lid lock first too;
- and, with or without the checkbox, when asked: `loginctl lock-session`
  (logind's Lock signal) or an app calling the ScreenSaver's `Lock`.

The lock is `zacos9-lock` (`shell/lock/lock.c`): every screen shows the
desktop pattern, and the main screen a Platinum dialog asking for the
password of the person logged in, checked through PAM — the
`zacos9-lock` service (`/etc/pam.d/zacos9-lock`, `@include common-auth`,
installed with ZacOS), or `login` if that file is missing, so a missing
file never locks anyone out. The check runs in a child process (PAM
pauses after a wrong password); the password is kept out of swap and core
dumps, and wiped once checked.

While locked, the compositor shows nothing but the lock, over black — no
window, desktop, menu bar or drag icon — and the screen saver, which goes
on playing above the lock on screens in its turn, so the screens lock
quietly beneath it and the dialog appears when they wake. Only the lock
gets the keyboard and the pointer; no keyboard shortcut works but
Ctrl+Alt+F*n* (a text console). If `zacos9-lock` dies without unlocking,
the screens stay locked and blank and it is started again a second later.
Unlocked, the keyboard goes back where it was.

`require-password` is checked again whenever the screens go idle, so
turning it off takes effect at once (a lock already in place stays until
unlocked).

## Apps that ask over D-Bus

Most video in a browser running under X11 (Chromium, Electron, so VS Code
too), VLC, and sandboxed (Flatpak) apps ask over D-Bus rather than with the
Wayland protocol. `zacos9-inhibit` (`shell/inhibit/inhibit.c`, started by
`zacos9-wm` in a real session) answers them:

- **`org.freedesktop.ScreenSaver`**, at `/org/freedesktop/ScreenSaver` and
  `/ScreenSaver`: `Inhibit` / `UnInhibit`, `SimulateUserActivity` (wakes
  every screen), `GetActive` (true while any screen shows the screen saver
  or is asleep), `Lock` (locks, answering once locked); `SetActive`,
  `GetActiveTime` and `GetSessionIdleTime` are accepted and do nothing yet.
- **The portal's Inhibit**, as the backend `zacos9-inhibit`
  (`share/xdg/xdg-desktop-portal/`): the idle flag keeps screens and the
  system awake, the suspend flag the system only, until the portal closes
  the request's `Request` object (the app asked, or quit).

Each request is passed to the compositor as `platinum_shell_v1.keep_awake`
(version 5), with the asking process (its D-Bus connection's PID) or, from
the portal, the app id. **The screens showing that app's windows stay
awake** — its windows are found by that process or one of up to seven of
its parents (X11 windows by `_NET_WM_PID`), or by app id — and the other
screens sleep as usual. When no window of it is found, every screen stays
awake. Any request keeps the system awake. A request ends when its app
calls `UnInhibit` or leaves the bus, and all of them if `zacos9-inhibit`
exits (its Wayland connection closes).

It starts only in a real session (or with `$ZACOS9_INHIBIT` naming it),
since a nested or test session would take the host's bus names. If
another desktop's service already owns `org.freedesktop.ScreenSaver`, it
says so and leaves it alone.

`ext-idle-notify-v1` is offered too, so other programs (an idle daemon, a
chat app's "away") can watch for idle; it reports inhibited while a screen
is kept awake.

## For tests

Each screen's state is published in
`$XDG_RUNTIME_DIR/zacos9-idle-$WAYLAND_DISPLAY`, rewritten when a state
changes or a screen comes or goes:

```
output HDMI-A-1 awake
output eDP-1 saver
```

States are `awake`, `saver` and `asleep`. `$ZACOS9_IDLE_MINUTE_MS` makes a
"minute" shorter, and `$ZACOS9_SUSPEND_COMMAND` replaces
`systemctl suspend` (and allows it without a real session; empty disables
it). `compositor/tests/test_idle.py` uses both: two headless screens with
400 ms minutes; `test_inhibit.py` does the same for the D-Bus requests,
under `dbus-run-session`, and `test_lock.py` for the lock, with
`test-lock` (the same program built with `-DZACOS9_LOCK_TEST`, taking
"sesame" instead of asking PAM; nothing like it is in `zacos9-lock`).

## Not yet

- **`ActiveChanged`** isn't sent yet, and X11 apps using the X screen
  saver extension (`XScreenSaverSuspend`) or `xset s` go unheard: Xwayland
  keeps those to itself.
- **More screen savers**, and perhaps xscreensaver's hacks as extra ones.
- **Virtual keyboards** (`zwp_virtual_keyboard`, used by `vptr` and
  on-screen keyboards) can type into the lock like any keyboard: any
  program in the session could guess passwords at PAM's pace.

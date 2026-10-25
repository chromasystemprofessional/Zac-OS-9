# Touchscreens and the on-screen keyboard

## Fingers (`compositor/src/touch.c`)

Apps that handle touch themselves get it directly: GTK apps, Firefox,
Chromium, and Qt apps from outside ZacOS. A swipe scrolls, a pinch zooms,
and a drag drags.

Everywhere else, the first finger acts as the mouse: window frames, the
desktop, the menu bar, X11 apps, and ZacOS's own programs. The pointer
jumps to the finger and the button goes down. Press-drag-release menus,
icon drags and spring-loaded folders work as they do with a mouse.

**A two-finger tap is a right-click**, where the first finger went down
(`touch_tap.c`). To tell it from a press, the first finger's press waits
up to 150 ms for a second finger. It goes at once if the finger moves
more than 12 px or lifts. Two fingers down together and lifted within
500 ms, without moving, are the right-click. Any other extra finger is
ignored. Test: `touch-tap`.

A touchscreen covers one screen: the one udev's `WL_OUTPUT` names, else
the screen of the same size as the glass, else the built-in panel, else
the main display.

## The on-screen keyboard (`compositor/src/osk.c`, `shell/keyboard/keyboard.c`)

The keyboard appears along the bottom of the screen when a text field has
the keyboard focus, **a touchscreen is attached, and no keyboard is**. It
goes when the field loses the focus. A computer with neither, such as a
screen-sharing host or a test session, doesn't get one.

- **"A keyboard is attached"** means a device that isn't virtual and has
  letter keys. A power button, a lid switch or a laptop's "Video Bus"
  doesn't count. In a nested session, the host's keyboard counts.
- **"A text field has the focus"** comes from text-input-v3. zacos9-wm
  tells each program's text input which of its surfaces has the keyboard
  focus. The program enables it while a field there has the focus: Qt,
  GTK, and so Firefox.
- **ZacOS's own programs** also say so themselves over
  `platinum_shell_v1.text_field` as their focus moves (`platinumShellInit`).
  Qt disables text-input only when the focus leaves a window, not when it
  moves from a field to a button inside one. A `PanelEdit` sets
  `WA_InputMethodEnabled` and the `zacos9TextField` property on its window
  while it has the focus.
- **Moving from field to field:** when a field loses the focus, the hide
  waits 300 ms, so going from one field to the next doesn't flicker.
- **The setting:** `onscreen-keyboard=true` in desktop.conf shows it for
  every text field, keyboard or not. `onscreen-keyboard=false` never shows it.

The compositor starts `zacos9-keyboard` when the keyboard is wanted and
writes `$XDG_RUNTIME_DIR/zacos9-keyboard-$WAYLAND_DISPLAY` ("shown 1" or
"shown 0"). The keyboard watches that file and leaves when it says 0.

The keyboard is a bottom layer surface that never takes the keyboard
focus, so the field keeps it. It reserves its height for windows that are
zoomed or placed later. Windows already open aren't moved, so a field low
on the screen can end up behind the keyboard.

- **Typing:** it types through virtual-keyboard with the same layouts as a
  real keyboard (`kblayout.c`, the one in use first). Each key shows what
  it types right now, so Shift shows capitals.
- **Modifier keys:** Shift, Option, Control and ⌘ stay down for the next
  key, or until you tap them again.
- **Fingers and holding:** each finger is its own key, since zacos9-wm
  gives this surface touches directly, and the mouse works too. Keys go
  down when touched and up when let go, so holding one repeats it.
- **Hide:** the **hide** key puts it away until a field next takes the
  focus.

Test: `onscreen-keyboard`. It runs a headless session with no keyboard
(and `onscreen-keyboard=true`, since it has no touchscreen either),
using a Qt window with a field. It checks that the keyboard appears and
types, that Shift lasts for one key, that closing the window hides the
keyboard, and that in a ZacOS window (`test-field-client`) Tab to a button
hides it.

**Not yet covered:**

- the lock screen's password (`shell/lock/lock.c` is a plain Wayland
  client with no text-input);
- the Finder's rename box (`LabelEditor`);
- moving a window so its field stays above the keyboard;
- a convertible laptop's tablet-mode switch (its keyboard counts as
  attached even when folded back).

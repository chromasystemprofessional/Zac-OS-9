# The Collar

The Collar (`zacos9-collar`) is ZacOS's desktop control strip. Its frame is
drawn pixel-for-pixel from the open and closed reference screenshots supplied
with the 2026 correction. Its modules use original ZacOS icons and modern Linux
services.

## Frame

All measurements are logical pixels and scale by nearest-neighbour integer factors.

- The strip sits flush in the bottom-left corner of the main display by
  default. It is 24 pixels tall.
- Open layout, from left to right:
  - a 14-pixel close box, then a black divider;
  - a 13-pixel recessed left scroll button, then a black divider;
  - module cells starting at x=29, each a 30-pixel beveled cell with a black
    divider (so each module adds 31 pixels);
  - a 13-pixel right scroll button;
  - an 18-pixel angled grip.

  The open width is `60 + 31 × modules`.
- Closed, only the 18-pixel grip remains. Its transparent corners are
  excluded from pointer input.
- Scroll arrows grey out at the ends of the module list.

## Interaction

- The close box folds the strip to the grip. Clicking the grip when closed
  unfolds it. Folding and unfolding play the current sound set's
  `window-collapse` and `window-expand` sounds.
- Dragging the grip horizontally resizes the visible module area in 31-pixel
  steps. The arrows scroll through hidden modules.
- **Command-drag** (hold Command and press anywhere on The Collar) moves it
  vertically. It can travel from the bottom of the screen up to just below
  the menu bar. Escape cancels the move and restores the previous position.
  The drag loop and drop use the sound set's window-drag sounds.
- Modules open their menu on mouse-down for press-drag-release selection; a
  quick click leaves the menu open. The pressed cell stays recessed while its
  menu is open.
- Menus open upward when they fit between The Collar and the menu bar, and
  otherwise open downward.
- Keyboard: Left/Right select a module; Return, Enter or Space opens it.
  Escape dismisses a menu, cancels a move, or folds the strip.
- Settings persist in `~/.config/zacos9/collar.conf`:
  - `collapsed`: whether the strip is folded.
  - `modules`: the number of visible modules; 0 means all of them.
  - `offset`: the distance from the bottom of the screen, in logical pixels.

## Modules

Every menu starts with the name of its device. Devices that need their own
controls get their own icons, so two displays give two Resolution modules and
two Brightness modules. Their menus are headed "Display 1: NAME (Main)" and
"Display 2: NAME".

| Module | Quick controls | Backend |
|---|---|---|
| Sound | Volume levels, mute, output selection, Sound control panel | `SoundClient` via `pactl` |
| Sound Set | Every installed sound set, with the current one checked; Appearance control panel | ZacOS sound-theme catalog |
| Network | Status, Wi-Fi on/off, Network Browser, TCP/IP control panel | NetworkManager (system D-Bus) |
| Bluetooth | Adapter power, Bluetooth control panel | BlueZ (system D-Bus) |
| Resolution (one per display) | The display's modes, with the current one checked; Monitors control panel | Compositor outputs file; `display.NAME.resolution` setting |
| Brightness (one per display) | 10/25/50/75/100%; Monitors control panel | Built-in displays: kernel backlight via logind. External displays: DDC/CI via `ddcutil` |
| Power | Battery level and state; confirmed Sleep | UPower and logind |
| Phone (one per KDE Connect device) | Battery and signal; Browse Device, Send Clipboard, Ring Device, Send Files, SMS Messages, Send Ping; the device's remote commands; pairing requests; KDE Connect Settings | `kdeconnectd` (session D-Bus) |
| Keychain | Every keychain, with the default checked (choose one to make it the default); Unlock/Lock each; Lock All Keychains; Keychain Access (Seahorse, if installed) | Secret Service (`gnome-keyring-daemon`, session D-Bus) |

### KDE Connect

If KDE Connect is installed (its daemon is running, or D-Bus can start it),
The Collar adds one Phone module per paired, reachable device after
Bluetooth. Each menu is headed by the device's name. Its icon is a phone
whose screen fills with the battery charge, in red below 15%. With
KDE Connect installed but no device in reach, one module remains, headed
"KDE Connect", with pairing requests and the settings.

Menu items appear only for the plugins the device has loaded:
- `battery`: battery level and charging state;
- `connectivity_report`: the cellular network type and signal;
- `sftp`: Browse Device;
- `clipboard`: Send Clipboard;
- `findmyphone`: Ring Device;
- `share`: Send Files...;
- `sms`: SMS Messages..., if `kdeconnect-sms` is installed;
- `ping`: Send Ping;
- `remotecommands`: the device's commands, then Add Commands...

The client (`shell/collar/kdeconnect.cpp`) follows the daemon's and devices'
D-Bus signals. It updates 150 ms after the last one, and also every five
seconds. `ZACOS9_KDECONNECT_SERVICE` overrides the service name, which tests
use.

### Keychain

Mac OS 9 had a Keychain Strip control strip module. The Collar's Keychain
module does the same job for the Secret Service keychains that apps such as
Chromium, Electron apps (ChatGPT, VS Code) and GNOME apps use. It appears
after Bluetooth and KDE Connect whenever `org.freedesktop.secrets` answers.
Its icon is a padlock with a key. The shackle is open while the default
keychain is unlocked.

ZacOS logs in automatically, so the login password never reaches
`pam_gnome_keyring` and the keychains start locked. The first app that needs
a secret makes gnome-keyring show its "Unlock Keyring" prompt, and the app
waits until the prompt is answered. ChatGPT, for example, opens nothing until
then. Unlock... in The Collar shows the same prompt ahead of time, and
Lock/Lock All Keychains lock them again. The session keyring, an unnamed
keyring kept only in memory, isn't listed.

The client (`shell/collar/keychain.cpp`) follows the service's
`CollectionCreated`/`CollectionDeleted`/`CollectionChanged`, `PropertiesChanged`
and prompt `Completed` signals. It updates 150 ms after the last one, and also
every five seconds. `ZACOS9_SECRETS_SERVICE` overrides the service name, which
tests use.

### Sound sets

The Collar plays the active sound set for its own actions: `menu-open` and
`menu-command` for menus, and the window collapse, expand and drag sounds for
the frame. Choosing a set from the Sound Set module applies it immediately, so
the next Collar sound already uses it.

### Long lists

The sound-set and resolution lists can be longer than the screen. Such a list
is trimmed to a window centred on the checked entry. "More Sound Sets..." or
"More Resolutions..." follows the list and opens the full control panel.

### Displays

Resolutions come from the compositor's
`$XDG_RUNTIME_DIR/zacos9-outputs-$WAYLAND_DISPLAY` file. Choosing a mode
writes the same per-output setting that the Monitors control panel uses.

**Built-in panels** (`eDP`, `LVDS` and `DSI` connectors) use their kernel
backlight. The Collar writes it through the caller's logind session, never as
root.

**External monitors** use DDC/CI VCP feature 0x10 (luminance) through
`ddcutil`, matching connectors from `ddcutil detect` to I2C buses. Commands for
one monitor are queued so they never overlap. A monitor that lacks DDC/CI, or a
system without `ddcutil`, shows the reason in the menu, and its brightness
levels are disabled.

`ddcutil` is a recommended package and is in the live-image package list.
Debian's package loads `i2c-dev` and installs the udev rule that gives users
access.

### Failures

Status refreshes every five seconds and after each command. A failed command
shows a Platinum alert. Background refresh failures appear only as disabled
menu entries.

## The Collar control panel

`zacos9-collarpanel` (Apple menu > Control Panels > Collar) follows the
Control Strip control panel. Its layout is measured from the original: a
295 × 248 window on white with two group boxes. The preview is The Collar's
own artwork.

- **Show/Hide.** Show Collar, Hide Collar, or Hot key to show/hide, saved
  as `collar-visibility` (default `show`; `hide`; `hotkey`) in
  `desktop.conf`. "Define hot key..." records a new key; "Current hot key"
  shows it.
- **Font Settings.** The font of the module menus, saved as
  `collar-menu-font` (default `system`; `views`). Our bitmap fonts each come
  in one size, so Size shows only that size.

The Collar watches `desktop.conf` and applies changes immediately.

### Hot key

`collar-hotkey` stores the hot key as modifier tokens and an xkb keysym name
joined by `+`. The default is `command+F8`, as in Mac OS 9. The tokens are
`command` (the ⌘/Super key), `option`, `control` and `shift`. A function key
works alone; any other key needs a modifier other than Shift.

`zacos9-wm` matches the hot key (`compositor/src/hotkey.c`) only when
`collar-visibility=hotkey`. A match is consumed, and the compositor runs
`zacos9-collar --toggle`, which asks the running Collar over D-Bus to hide or
show itself. That state persists in `collar.conf` (`hidden`).

Clients receive ⌘ as Ctrl (see `command_as_ctrl` in `input.c`). The panel
therefore can't tell ⌘ from Ctrl, and records both as `command`, so `command`
also matches Ctrl held instead of ⌘. The `control` token, for hand-edited
settings, matches only Ctrl.

## Integration

`zacos9-wm` launches The Collar alongside the menu bar and Finder.
`ZACOS9_COLLAR=''` disables it; any other nonempty value overrides the command.

The Collar owns `org.zacos9.Collar` on the session bus, which prevents a second
copy from starting. It is a layer-shell surface anchored bottom-left on the
main display. It reserves no desktop space and takes no exclusive keyboard
focus. Its menus are xdg popups attached to that layer.

## Tests

```sh
scripts/build.sh shell/zacos9-collar shell/test-collar shell/test-collar-controls \
  shell/test-collar-displays shell/test-collar-kdeconnect shell/test-collar-keychain \
  shell/test-collar-panel \
  compositor/test-hotkey compositor/zacos9-wm shell/zacos9-capture shell/vptr
meson test -C build --no-rebuild --num-processes 1 collar-ui collar-controls \
  collar-displays collar-kdeconnect collar-keychain collar-panel collar-hotkey collar-native
```

- `collar-ui` checks:
  - the reference pixels of the open and closed frames;
  - module hitboxes and the input mask;
  - folding, resizing and scrolling;
  - Command-drag limits, persistence and Escape;
  - each module's labelled menu and its placement;
  - keyboard control.

  `test-collar --save-image PATH` also writes `PATH` (open) and
  `PATH-closed.png` for visual review.
- `collar-controls` tests the network, Bluetooth and power code against
  isolated D-Bus stand-ins.
- `collar-displays` uses a fake logind, fake DRM and backlight trees, and
  `tests/fake-ddcutil.sh`. It covers display labels, modes, resolution
  switching, backlight and DDC/CI brightness, and their failures.
- `collar-kdeconnect` runs a fake `kdeconnectd` on a private session bus. It
  covers Phone modules appearing and disappearing, device labels, battery,
  signal and commands, icon pixels, the menu actions, pairing, and
  show/hide/hot-key visibility and the menu font.
- `collar-keychain` runs a fake Secret Service on a private session bus. It
  covers the module appearing and disappearing, the keychain list without the
  session keyring, the padlock's pixels, Unlock through the service's prompt,
  choosing the default keychain and Lock All Keychains.
- `collar-panel` checks the control panel's pixels against the measured
  layout, its settings, and the hot key recorder.
- `collar-hotkey` checks how the compositor matches hot keys.
- `collar-native` runs a private headless compositor at 1× and 2× to check real
  placement, folding, input and popups. It also presses ⌘F8 through the
  compositor to hide and show The Collar. Its environment points
  `ZACOS9_KDECONNECT_SERVICE` and `ZACOS9_SECRETS_SERVICE` at missing names, so
  an installed KDE Connect or keyring can't add modules.

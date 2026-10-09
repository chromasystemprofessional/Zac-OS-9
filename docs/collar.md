# The Collar

The Collar (`zacos9-collar`) is ZacOS's compact desktop control strip. It uses
original ZacOS pixel artwork, the shared Platinum palette and menu painter,
and modern Linux services rather than emulating classic control-strip modules.

## Screenshot-matched frame

- Bottom-left of the main display, four logical pixels above its bottom edge.
- 24 logical pixels tall; 219 pixels wide with the five modern modules exposed.
- Angled 17-pixel grip with an inset handle and stippled grip marks; its
  transparent corners are excluded from pointer input.
- 32-pixel beveled module cells, each with a 16-pixel original ZacOS icon and a
  separate solid menu triangle. Scroll buttons are 14 pixels wide with hollow
  arrows; the outer 14-pixel end tab is square.
- Neutral `#C0C0C0` strip face, black outlines, white highlights and `#808080`
  shadows, distinct from the lighter shared Platinum menu face.
- Nearest-neighbour integer scaling; folding leaves the 17-pixel angled grip.
- Click either end to fold; click the folded tab to unfold.
- Drag the outer end horizontally to resize the visible module area; the
  small arrows scroll through the hidden modules without scaling the artwork.
- Alt-drag moves the strip vertically along the left edge.
- Left/Right select modules; Return or Space opens the selected module.
  Escape folds the strip or dismisses an open menu.
- Module menus open on mouse-down for press-drag-release selection; a quick
  click leaves the menu open, using the existing Mouse panel's click timing.
- Folded state, visible module count and vertical position persist in
  `~/.config/zacos9/collar.conf`.

Frame proportions come from the user's reference screenshot supplied on
2026-10-09: about 54 image pixels of strip height, a 71-72-pixel repeated
module pitch, a 38-pixel grip and 31-32-pixel scroll/end controls. These are
consistent with approximately 2.24x enlargement of a 24/32/17/14-pixel layout.
The image is rescaled and compressed, so exact source-pixel colors and font
rasters cannot be recovered or claimed as a byte-identical match. The frame
uses clean one-pixel edges instead of reproducing compression artifacts.

Icons remain original ZacOS artwork for the five modern functions, not the
reference's Apple icons or legacy module lineup. The reference wallpaper is
not included. Control-panel shortcuts lead their module menus, as in the
reference, while text and menus retain the existing ZacOS font and painter.
Open modules reverse their bevel until the popup closes. Right-edge docking,
module rearrangement and third-party modules are not implemented in this pass.

## Modern functions

| Module | Quick controls | Backend |
|---|---|---|
| Volume | Seven volume levels, mute/unmute, output/port selection, Open Sound | Existing `SoundClient`, through `pactl` |
| Network | Connection status, Wi-Fi on/off, TCP/IP and Network Browser shortcuts | NetworkManager system D-Bus |
| Bluetooth | Adapter power, Open Bluetooth for pairing and device management | BlueZ system D-Bus |
| Brightness | 10/25/50/75/100% backlight levels, Open Monitors | Read `/sys/class/backlight`; write through the caller's logind session |
| Power | Battery percentage and charging state, confirmed Sleep | UPower display device and logind |

Status is refreshed asynchronously every five seconds, including while the
strip is folded. Commands reread actual state after completion; a denied
operation never displays invented success. Service errors and missing hardware
are logged and shown as disabled status/menu entries. User-command failures
also show a Platinum alert, while background polling failures do not repeatedly
interrupt the desktop.

Brightness requires a kernel-exposed display backlight; external monitors
without one remain unavailable rather than getting a fake slider. Backlight
writes use logind, not root permissions or direct sysfs writes. At least one
hardware step is retained to avoid blacking out the display. Bluetooth controls
the first available adapter; advanced device operations stay in its existing
control panel. AC-only computers display "AC power (no battery)" when UPower
confirms no battery. Missing UPower is reported separately. Sleep is offered
only when logind allows it, including its normal authorization flow.

## Integration

`zacos9-wm` launches The Collar alongside the menu bar and Finder, using the
same adjacent-binary/build-tree/PATH lookup. `ZACOS9_COLLAR=''` disables that
launch; a nonempty value overrides the command for development. The component
owns `org.zacos9.Collar` on the session bus to prevent duplicate strips.

Its `zacos9-collar` layer-shell surface stays on the main display above ordinary
windows, without reserving desktop space or taking exclusive keyboard focus.
Menus are actual xdg popups attached to that layer. The compositor configures
and constrains layer popups through the same lifecycle as ordinary window
popups; a parentless xdg popup awaits its layer-shell attachment rather than
being mistaken for a window popup.

UPower is a package dependency and is included in the live-image package list.
NetworkManager is already included in the live image. Sound needs
`pulseaudio-utils` and a compatible sound server; BlueZ supplies Bluetooth.
No new polkit rules are installed.

## Tests

```sh
scripts/build.sh shell/zacos9-collar shell/test-collar \
  shell/test-collar-controls shell/test-sound-output \
  compositor/zacos9-wm shell/zacos9-capture shell/vptr
meson test -C build --no-rebuild --num-processes 1 \
  collar-ui collar-controls collar-native sound-output
```

- `collar-ui`: exact strip/tab bounds and bevel pixels, module hitboxes,
  transparent-corner input masking, resize pitch, persistence, scrolling,
  Alt-drag, keyboard interaction, pressed bevel restoration and disabled commands.
- `collar-controls`: isolated D-Bus stand-ins and backlight files; real command
  signatures, authorization denial, missing services/hardware and malformed
  status. It never changes a live device or actually suspends the computer.
- `collar-native`: private headless compositor at 1x and 2x; captures committed
  pixels to verify placement, outline and integer scaling, and uses the existing
  `vptr` helper for genuine Wayland input, popup placement and Escape dismissal.
  It also checks folded startup and duplicate-instance rejection.
- `sound-output`: existing sound-output regression suite, including separate
  user-command versus background failure notification.

The pixel assertions lock the screenshot-derived frame dimensions and clean
bevel/arrow pixels. Native captures additionally check actual 1x/2x surface
placement. They do not claim byte-for-byte equality against the enlarged,
compressed screenshot, or substitute copied reference artwork for ZacOS icons.

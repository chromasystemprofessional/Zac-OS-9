# Other people's applications in ZacOS 9: plan and status

How far third-party programs (GTK, Qt, Electron, ...) look and behave like
Mac OS 9 Platinum, what is done, and what to do next. Written to resume
from: read this, then `PROJECT_STATUS.md` for the latest entries.

## The approach

Applications can't be forced to look right; each layer is covered with what
works for it:

| Layer | Who draws it | How we cover it | State |
|---|---|---|---|
| Window frame | zacos9-wm (server-side decorations) | session asks toolkits not to draw their own | done |
| Controls (buttons, fields, scroll bars) | the toolkit | a Platinum theme/style per toolkit | GTK 3, Qt 6 done |
| Menus | the application, inside its window | global menu: the app's menus in our menu bar | not started |
| Everything | Electron, GTK 4 / libadwaita | nothing reliable | known gap |

## Done

- **Session defaults** (`session/zacos9-session`): `GTK_CSD=0`,
  `QT_WAYLAND_DISABLE_WINDOWDECORATION=1`, `GTK_THEME=ZacOS9`,
  `QT_STYLE_OVERRIDE=zacos9`. GTK 3 already took our frames before this;
  it is a safeguard.
- **GTK 3 theme** `share/themes/ZacOS9/gtk-3.0/gtk.css`, installed to
  `/usr/share/themes`. Adwaita imported as the base, then restyled: grey
  outlined buttons, square corners, sunk white fields, flat grey menu bar and
  menus, lavender selection, Platinum tabs, check boxes and scroll bars, no
  shadows or animations. Checked on screen with Mousepad (menu bar, File
  menu, Preferences dialog). Not checked: GIMP/Inkscape dialogs.
  To try it from a build tree: `XDG_DATA_DIRS=$PWD/share:/usr/share`.
- **Qt 6 style** `shell/qtstyle/` -> `<libdir>/qt6/plugins/styles/zacos9style.so`.
  A `QProxyStyle` over Fusion with a Platinum palette; push buttons (and the
  default ring), check boxes, radio buttons, text-field frames and scroll bars
  are drawn by `lib/widgets.c`, so they match our own programs pixel for pixel.
  Scroll bar geometry is in `subControlRect` (arrows at both ends, fixed
  17-px thumb). Checked with `build/qtdemo.cpp` (thumb drag and arrows work).
  To try it from a build tree: copy `build/shell/zacos9style.so` into
  `<dir>/styles/` and set `QT_PLUGIN_PATH=<dir> QT_STYLE_OVERRIDE=zacos9`.
- **X11 frame re-fit** (`compositor/src/xwayland.c`, `set_geometry`): for
  VS Code's resize glitch (the frame kept the old size after an outline
  resize). Unconfirmed by the user.

## Next: global menus (the biggest visible difference left)

Goal: an application's menus (File, Edit, ...) appear in ZacOS 9's menu bar
when its window is in front, and its in-window menu bar disappears, as on a
Mac. Applications without support keep their own menus.

The standard mechanism is D-Bus:
- A registrar service, `com.canonical.AppMenu.Registrar`
  (`RegisterWindow(windowId, menuObjectPath)`), which our menu bar would own.
- Each application exports its menu as `com.canonical.dbusmenu` at that path;
  the menu bar reads the layout (`GetLayout`), shows it, and sends `Event`
  ("clicked") back.

Steps:
1. **Find out what works on Wayland in Debian 13 first.** The registrar keys
   windows by X11 window id. On Wayland:
   - GTK 3: `appmenu-gtk3-module` (Debian package of that name) exports
     dbusmenu; check how it identifies a Wayland window (it may only work
     under XWayland).
   - Qt 6: Qt has built-in dbusmenu export (`QDBusMenuBar`, used by its
     generic Unix platform theme when the registrar exists); on Wayland KDE
     pairs it with the `org_kde_kwin_appmenu` protocol to say which surface a
     menu belongs to. Check whether Qt 6.8 sends that without KDE's
     integration plugin.
   - Write down which toolkits can be supported before building anything.

   **Findings (2026-10-04, on the installed system: Debian 13, Qt 6.8.2,
   GTK 3.24, appmenu-gtk3-module 25.04).** Tested with a stand-in
   registrar (owns the name, logs `RegisterWindow`, then calls `GetLayout`)
   and two small test windows with a File and an Edit menu.
   - **Qt 6: works, on Wayland and X11, with nothing extra installed.** As
     soon as the registrar name is owned, Qt registers `/MenuBar/N` and
     **hides its own in-window menu bar** (`isNativeMenuBar()` is true), so
     the menu bar must own the name only when it can show the menus.
     `GetLayout` returned File and Edit. On Wayland the window id is
     meaningless (`winId()` = 1, a counter); on X11 it is the real XID. So on
     Wayland, menus are matched to windows by the **D-Bus sender's PID**
     (`GetConnectionUnixProcessID`) against the front window's client PID.
     Qt registers twice while a window opens (unregister, register a new
     path); the first `GetLayout` timed out because the app was still busy
     opening - fetch asynchronously and use the latest registration.
   - **GTK 3 (`appmenu-gtk3-module`): not on Wayland without more work.** On
     Wayland the module ignores the registrar and calls
     `gdk_wayland_window_set_dbus_properties_libgtk_only`, i.e. GTK's
     private `gtk_shell1` protocol (`gtk_surface1.set_dbus_properties`),
     which zacos9-wm doesn't implement (it logged a GDK critical and did
     nothing). The menus it exports there are `org.gtk.Menus` /
     `org.gtk.Actions` (GMenuModel), not dbusmenu - a second format to read.
     Under X11 it saw the registrar but never registered (not investigated).
   - **GTK 4 / libadwaita, Electron**: no global menu support on Wayland;
     they keep their own menus.
   - **What the menu bar is missing:** it learns the front window's app id
     from foreign-toplevel (`shell/menubar/toplevels.c`) but not its PID.
     Proposed: `platinum_shell_v1` version 2 gets an event giving the front
     window's client PID (`wl_client_get_credentials` in zacos9-wm).

   **Decision:** build Qt 6 first (registrar + dbusmenu + PID from
   zacos9-wm). GTK 3 (`gtk_shell1` + GMenuModel) is a separate, later step.
2. **Menu bar side** (`shell/menubar/`, C): own the registrar, track the
   front window's app id / surface, fetch its dbusmenu layout, draw it with
   the existing menu drawing (`lib/menudraw.c`), forward clicks. If the
   compositor must tell the menu bar which window is in front, the
   foreign-toplevel handles it already uses (`toplevels.c`) may be enough;
   a surface-to-menu mapping may need `org_kde_kwin_appmenu` in zacos9-wm.
3. **Keep the Apple menu and the application menu** (right side) as they are.
4. Test with a Qt 6 program (GTK 3 after the separate GTK step).

## Gaps (known, not planned yet)

- **GTK 4 / libadwaita** ignore themes and draw their own title bars.
- **Electron** (VS Code, ...) draws everything itself; only its frame and
  fonts can be influenced.
- **Qt 5 programs**: the style plugin is Qt 6 only (Debian 13 still has some
  Qt 5 applications). The same source should build against Qt 5.
- **GIMP** keeps its own dark theme until Preferences > Theme is "System";
  the GIMP preset (`appstore/zacos9-appstyle`) could set that.
- **Accent colour**: the GTK theme's lavender is fixed; the Qt style reads
  the Appearance panel's accent at start-up only.

## Testing notes

- Nested session: `build/wintest/nested.sh SCRIPT` (one screen) or
  `nested2.sh` (two), screenshots with `grim`, pointer and keys with
  `build/shell/vptr` (use `V=build/shell/vptr` after `cd` to the repo; the
  path has spaces).
- Real hardware differs: a 2007 iMac's Radeon locks up under GL, so the
  session draws with pixman on the `radeon` driver (see PROJECT_STATUS.md).

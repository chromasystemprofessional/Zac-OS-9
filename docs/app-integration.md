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
| Controls and standard prompts | the toolkit | a Platinum theme/style per toolkit | GTK 3 and Qt 6; GTK 4 stylesheet added, unverified |
| Menus | the application, inside its window | global menu: the app's menus in our menu bar | Qt 6, GTK 3 Wayland and Electron (under X11) implemented |
| App-owned UI | Electron, libadwaita, custom-drawn dialogs | no reliable override | known gap |

## Done

- **Picture Viewer** (`shell/picture/`, `zacos9-picture`): images
  double-clicked in the Finder open here; `share/xdg/zacos9-mimeapps.list`
  makes it the default for image types. A user's own `~/.config/mimeapps.list`
  still wins. It opens a picture fitted to the screen (never enlarged), sizes
  its window to the picture (zacos9-wm exempts it from the near-full-screen
  default frame), and zooms in fixed steps from 1/16 to 16× with the bar's
  − / + buttons, ⌘+ / ⌘−, ⌘0, ⌘9 or ⌘-scroll. Drag to pan.
- **Standard application file chooser**: the session selects the ZacOS
  `FileChooser` portal for GTK and Qt standard dialogs. Its first screen lists
  Zacintosh HD, mounted data volumes and Desktop folder aliases, without a
  places sidebar; the subsequent file browser also has no sidebar. The
  Zacintosh HD shows the Finder's Macintosh view, not the Unix root: System
  Folder, Applications, Home and the user's own top-level folders. The backend
  builds this as a read-only tree under `$XDG_RUNTIME_DIR/zacos9-file-chooser`
  for each request. Real folders behind the view (Home, Appearance, Fonts,
  Preferences, user folders) are links to those folders, and each installed
  application appears by name as a link to its `.desktop` launcher. Generated
  listings (Control Panels, Extensions, Unix) are omitted. Items can only be
  chosen or saved inside the real folders; the view's own folders are browse
  only. Applications receive real paths, never the chooser's links. Building
  the view requires the Finder's application scan (about 5 seconds the first
  time), so the backend warms up at start and the session starts the backend at
  login. Requests arriving during the scan wait for it; closing one cancels it
  immediately. Every request starts
  at the location list, ignoring app-suggested Home/current folders. Desktop
  folder aliases appear under their Desktop names with italic labels and
  Platinum folder icons. They may point to any directory, including mounted
  AFP or Windows/SMB shares and their subfolders; document aliases are omitted.
  Choosing one opens its resolved folder, and open, folder and save requests
  return real target paths. Desktop folder aliases can also be followed from
  Home/Desktop inside Zacintosh HD. Missing or changed aliases report an error
  rather than falling back to Home; repair them using Finder's Fix Alias.
  This refinement was installed in the live session on 2026-10-08 and is
  included in 0.1.38. The restarted backend responds on D-Bus;
  the user confirmed the live AFP disk works after remounting. Live SMB
  selection still needs confirmation.
  The browser stays within the selected drive or alias target: Unix ancestors
  are not offered, and typed paths or symlinks outside that location cannot be
  selected. Cancel and reopen to choose another location. This is a navigation
  restriction, not an application sandbox. App-specific custom pickers are not rewritten.
  Read-only drives remain available for opening files. Mac/APFS drives are
  discovered from active mount records with their Desktop names, even when
  GIO or Qt storage enumeration omits their FUSE mounts. AFP and kernel SMB
  disks are also discovered from active mount records, preserving spaces in
  their names and listing them even when an I/O error prevents Qt from
  recognizing them as ready. GVFS Windows shares use their local GVFS paths
  and share names, since GIO normally reports their roots as SMB URLs.
  The correction was installed live on 2026-10-08; the live location screen
  was visually verified to include the mounted AFP disk `adam's home`.
  An unavailable drive
  reports an error instead of falling back to Home. Drive icons are rendered by
  the same Platinum painter as the Finder Desktop: the 32-pixel disk icon,
  nearest-neighbour scaled for HiDPI, with the Desktop's USB badge on USB
  volumes. A rejected outside item is cleared from the selection so a later
  valid choice is accepted. The Macintosh view and the login warm-up have
  offscreen regressions but have not yet been confirmed in the live session.
  The 0.1.36 backend incorrectly rejected real D-Bus option dictionaries,
  including VS Code's requests. The correction decodes those dictionaries and
  nested arrays, supports request cancellation, and keeps the backend alive
  between dialogs. Regression tests exercise real D-Bus file/folder/save
  requests, multi-file save names, cancellation, and repeated backend use.
  Automated tests use an offscreen Qt display; they do not establish visual
  behavior across all GTK/Qt/Electron applications.
  After installing the correction, the user confirmed live VS Code file/folder
  selection works, Home files are accessible through Zacintosh HD, and both
  mounted ChromaSystem Professional drives appear. Other applications and
  saving to read-only drives have not been live-tested.
  See [Desktop files and aliases](vfs.md#desktop-files-and-aliases).
- **Session defaults** (`session/zacos9-session`): `GTK_CSD=0`,
  `QT_WAYLAND_DISABLE_WINDOWDECORATION=1`, `GTK_THEME=ZacOS9`,
  `QT_STYLE_OVERRIDE=zacos9`, and GTK/Qt portal selection. GTK 3 already took
  our frames before this; it is a safeguard.
- **GTK 3 theme** `share/themes/ZacOS9/gtk-3.0/gtk.css`, installed to
  `/usr/share/themes`. Adwaita imported as the base, then restyled: grey
  outlined buttons, square corners, sunk white fields, flat grey menu bar and
  menus, lavender selection, Platinum tabs, check boxes and scroll bars, and
  compact Platinum message dialogs with emphasized default actions. Checked
  on screen with Mousepad (menu bar, File menu, Preferences dialog); other
  applications' prompts have not been visually exercised.
  To try it from a build tree: `XDG_DATA_DIRS=$PWD/share:/usr/share`.
- **GTK 4 theme** `share/themes/ZacOS9/gtk-4.0/gtk.css` supplies the same
  palette, controls, and prompt treatment to GTK 4 applications which honor
  GTK themes. GTK 4 is not installed in the current build environment, so
  this stylesheet has not been runtime-validated. Libadwaita and applications
  that draw their own dialogs can ignore GTK themes.
- **Qt 6 style** `shell/qtstyle/` -> `<libdir>/qt6/plugins/styles/zacos9style.so`.
  A `QProxyStyle` over Fusion with a Platinum palette; push buttons (and the
  default ring), check boxes, radio buttons, text-field frames and scroll bars
  are drawn by `lib/widgets.c`, so they match our own programs pixel for pixel.
  Standard Qt dialogs and message prompts inherit the same palette and
  controls. Scroll bar geometry is in `subControlRect` (arrows at both ends,
  fixed 17-px thumb). Checked with `build/qtdemo.cpp` (thumb drag and arrows
  work); prompts are covered by an offscreen palette regression check.
  To try it from a build tree: copy `build/shell/zacos9style.so` into
  `<dir>/styles/` and set `QT_PLUGIN_PATH=<dir> QT_STYLE_OVERRIDE=zacos9`.
- **X11 frame re-fit** (`compositor/src/xwayland.c`, `set_geometry`): for
  VS Code's resize glitch (the frame kept the old size after an outline
  resize). Unconfirmed by the user.

## Global-menu implementation history

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
   That GTK step is now implemented below; these findings describe the
   original investigation, not the current implementation status.
2. **Menu bar side** (`shell/menubar/`, C): own the registrar, track the
   front window's app id / surface, fetch its dbusmenu layout, draw it with
   the existing menu drawing (`lib/menudraw.c`), forward clicks. If the
   compositor must tell the menu bar which window is in front, the
   foreign-toplevel handles it already uses (`toplevels.c`) may be enough;
   a surface-to-menu mapping may need `org_kde_kwin_appmenu` in zacos9-wm.
3. **Keep the Apple menu and the application menu** (right side) as they are.
4. Test with a Qt 6 program (GTK 3 after the separate GTK step).

## Global menus, Qt 6: built (step 2)

- `shell/menubar/appmenu.c` owns `com.canonical.AppMenu.Registrar` (only
  when zacos9-wm offers `platinum_shell_v1` version 2), keeps each
  program's latest registration, fetches `GetLayout` asynchronously and
  again on `LayoutUpdated` / `ItemsPropertiesUpdated`, drops a program's
  menus when it leaves the bus, and sends `Event(id, "clicked")` for a
  chosen item. GLib's main context runs inside the menu bar's `poll()`.
- zacos9-wm sends the front window's client pid
  (`platinum_shell_v1.active_client`, new in version 2; for an X11 window
  its own pid, not Xwayland's). `menus_rebuild` shows the front program's
  menus after the logo menu instead of the generic File/Edit; programs
  without them keep the generic ones.
- Shown: labels (mnemonics stripped), separators, disabled items, check
  and radio state, ⌘ shortcuts for Control+letter, one level of submenu
  (deeper ones are shown disabled). `AboutToShow` is sent once for each
  empty menu and submenu, then the layout is fetched again: Electron exports
  them empty and fills them only then. Not done: icons, the Quit
  item Mac programs have (a Qt program's File menu has its own or none).
- Tested in a nested session (`dbus-run-session`, a Qt 6 test window): its
  File and Edit menus appear in our bar and its own menu bar disappears;
  File > Open ran the program's action; Edit > More opens its submenu;
  after it quits the Finder's menus come back; foot keeps the generic
  menus. Debug: `ZACOS9_APPMENU_DEBUG=1` logs registrations and the
  front pid. Not yet tried with a real Qt program or on the installed system.

## Global menus, GTK 3 Wayland: implemented (0.1.15)

- The compositor implements the version-one `gtk_shell1`/`gtk_surface1`
  menu-property protocol. `platinum_shell_v1` version 4 sends the focused
  surface's unique D-Bus name and menu/action paths to the menu bar. This
  distinguishes windows in one process; version 2/3 clients remain compatible.
- The menu bar imports `GDBusMenuModel` and `GDBusActionGroup` exports,
  subscribes to menu/action updates, and renders sections, submenus,
  enabled state, check/radio state and action targets. It forwards `app.`,
  `win.` and legacy `unity.` actions. The bus owner's PID is checked
  asynchronously against the focused process before importing exports;
  stale clicks and unavailable owners do not activate another window's actions.
- Native GTK application menus use GTK's built-in exports. Legacy
  `GtkWindow`/`GtkMenuBar` applications, including Galculator, use the small
  `shell/gtkmenu/module.c` module and Debian's `libappmenu-gtk3-parser`.
  The stock appmenu module attempts to publish legacy Wayland metadata before
  the window is realized; this fails on the tested version 25.04. ZacOS's
  module instead exports after realization and publishes the correct action
  path. It reuses the maintained parser rather than implementing a second
  menu-widget parser.
- The session loads `GTK_MODULES=zacos9-appmenu` while preserving other
  user modules, and propagates its settings to D-Bus-activated applications.
  Legacy in-window menus hide only while the registrar is available, and
  reappear if it exits. On unsupported desktops the module leaves them alone.
- Tests run under a private D-Bus session and headless compositor. They
  verify actual Galculator File/Edit/Help titles in the global-menu backend,
  activate real legacy and native GTK actions through that backend, switch
  between two GTK windows in one process, restore menus on close, and verify
  in-window menu fallback. A real Qt fixture verifies existing registration
  and action forwarding. Backend tests cover live updates, checked/radio
  actions, owner disappearance, PID mismatches, invalid paths and stale
  activations.

The new compositor, menu bar, GTK module and session environment must be
installed together; log out/in and restart applications after updating.
GTK applications explicitly forced to X11, custom non-exported menus and
Electron are not covered by this GTK Wayland path.

An application window (`GtkApplicationWindow`) with a menu bar widget of its
own and no menu model for GTK - GIMP 3's image window - is exported by the
module like a plain window. GDK sends a window's menu properties once, when
it is first shown, with the last values given, and GTK gives its own (no
menu bar) when the window is realized; so the module connects after that
realize handler (from `GtkApplication::window-added`) and gives ours in
time, opening the session bus synchronously if it isn't open yet. Windows
whose application has a menu model keep GTK's own export. Checked in a
nested session with GIMP 3.0.4: its File ... Help menus in the menu bar, its
own menu bar hidden, File > New... opened the New Image dialog. Installed
live on 2026-10-10, the user confirmed GIMP's menus work.

## Global menus, Electron (VS Code, ...): implemented

Electron exports its menus (`com.canonical.AppMenu.Registrar` plus
dbusmenu, through `libdbusmenu-glib.so.4`, which it loads at run time) only
when it runs **under X11**. On Wayland it never does, on any desktop. So:

- **Electron apps start under X11.** `lib/electron.c` recognises an Electron
  app (`resources/app.asar` or `resources/app/` beside the binary, or one
  folder up from a launcher script in `bin/`) and adds
  `--ozone-platform=x11 --force-device-scale-factor=N` after the program,
  N being the screen's scale from zacos9-wm's outputs file. The Finder
  (`appdb.cpp`), Login Items (`autostart.cpp`, both through
  `finder/electronlaunch.h`) and the Apple menu (`menubar/launch.c`) do
  this. Started from a terminal, an Electron app picks Wayland as before.
- **The package depends on `libdbusmenu-glib4`**; without it Electron
  silently keeps its menus.
- **HiDPI X11 windows are sharp** (`compositor/src/xwayland.c`). An X11
  window whose program was started with `--force-device-scale-factor`
  equal to the screen's scale (read from `/proc/PID/cmdline`) gets a scale
  of its own: its X11 positions and sizes are in pixels, its buffer is shown
  at 1/scale, and pointer positions are multiplied back. Other X11 windows
  (Classic, Wine, xterm) have scale 1 and are enlarged as before. Override-
  redirect menus follow their program (`_NET_WM_PID`, else the front window).
- **Xwayland sees outputs in pixels** (`compositor/src/xwayland_output.c`):
  its own `zxdg_output_manager_v1`, hidden from every other client, while
  wlroots' manager is hidden from Xwayland. Without it the X11 root stays at
  the layout's size and Xwayland would keep the pointer out of the right and
  bottom of a scaled window.
- **Not every "Electron" app has the exporter.** The ChatGPT desktop app
  (`/usr/lib/chatgpt`, checked 2026-10-10) reports Electron 42 but runs on
  OpenAI's own runtime ("owl", Chromium 155) without Electron's Linux
  global-menu code (no `libdbusmenu-glib` loader, no
  `ELECTRON_FORCE_WINDOW_MENU_BAR`): it sets an application menu but never
  registers it, and its main window draws its menus in HTML. Nothing to
  read on our side short of scraping the window over AT-SPI.
- **The app must use a native menu bar.** VS Code does only with
  `"window.titleBarStyle": "native"` (its default custom title bar draws
  menus in HTML and never exports them). With the native title bar it has
  no in-window menu bar: Electron hides it once the registrar is there.

Tested (2026-10-10): VS Code 1.141.0 (Electron 43) under X11 on the live session
exported File ... Help and the menu bar showed them. In a nested zacos9-wm at
2x, VS Code opened at its normal size and sharp, a click landed on the icon
under the pointer, and the pointer stayed its normal size; xmessage (1x) looked
as before. Installed live, the menu titles appeared but the menus were empty
until the menu bar sent `AboutToShow` (above); with that, in a nested session,
File > New Text File opened a new editor. Not yet tried with another Electron
app.

Known costs: on every commit wlroots resizes a scene buffer to its surface
and xwayland.c resizes it back, so a scaled X11 window is fully redrawn each
frame (no partial damage). Changing the screen's scale needs the Electron app
restarted (its scale factor is fixed at launch). If an Electron app runs under
X11 without the scale argument it draws at 1x and is enlarged (blurry), as
before.

## Gaps (known, not planned yet)

- **GTK 4 / libadwaita** ignore themes and draw their own title bars.
- **Electron** (VS Code, ...) draws everything itself; only its frame,
  fonts and (under X11) its menus can be influenced.
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

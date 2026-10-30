# The desktop's programs, a folder each with its own meson.build (the
# subdir() calls below). First what they share: Wayland, the protocols,
# Qt and the libraries; last, the tests that run several of them with
# zacos9-wm.

# C++ for the Qt programs (the project's own language is C).
add_languages('cpp', native: false)
wayland_client = dependency('wayland-client')
xkbcommon_client = dependency('xkbcommon')
wayland_protos = dependency('wayland-protocols')
wayland_scanner = find_program(
  dependency('wayland-scanner', native: true).get_variable('wayland_scanner'),
  native: true,
)
wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')

client_protocols = [
  wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml', # layer-shell popups
  wl_protocol_dir / 'stable/viewporter/viewporter.xml',
  wl_protocol_dir / 'staging/fractional-scale/fractional-scale-v1.xml', # 1.25x, 1.5x, 1.75x
  meson.project_source_root() / 'protocols/wlr-layer-shell-unstable-v1.xml',
  meson.project_source_root() / 'protocols/wlr-foreign-toplevel-management-unstable-v1.xml',
  meson.project_source_root() / 'protocols/virtual-keyboard-unstable-v1.xml',
]
client_protocol_src = []
foreach xml : client_protocols
  name = xml.underscorify()
  client_protocol_src += custom_target(name + '_client_h',
    input: xml,
    output: '@BASENAME@-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  client_protocol_src += custom_target(name + '_c',
    input: xml,
    output: '@BASENAME@-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
endforeach

# GDBus: the menu bar's global menus (registrar, dbusmenu); GIO for the
# Finder's desktop entries too.
gio = dependency('gio-2.0')

# platinum-shell-v1: the menu bar (front window's pid) and the Finder.
finder_protocol = []
foreach kind : [['client-header', '@BASENAME@-client-protocol.h'], ['private-code', '@BASENAME@-protocol.c']]
  finder_protocol += custom_target('finder_platinum_shell_' + kind[0].underscorify(),
    input: meson.project_source_root() / 'protocols/platinum-shell-v1.xml',
    output: kind[1],
    command: [wayland_scanner, kind[0], '@INPUT@', '@OUTPUT@'],
  )
endforeach
# libpam directly: Debian's pam.pc also needs libaudit's, for nothing here.
pam = meson.get_compiler('c').find_library('pam', has_headers: ['security/pam_appl.h'])
qt6 = dependency('qt6', modules: ['Core', 'Gui', 'Widgets', 'Network', 'DBus'],
  private_headers: true) # QPlatformNativeInterface: a window's wl_surface
snapshot_protocol = []
foreach kind : [['client-header', '@BASENAME@-client-protocol.h'], ['private-code', '@BASENAME@-protocol.c']]
  snapshot_protocol += custom_target('zacos_snapshot_client_' + kind[0].underscorify(),
    input: meson.project_source_root() / 'protocols/zacos-snapshot-v1.xml',
    output: kind[1],
    command: [wayland_scanner, kind[0], '@INPUT@', '@OUTPUT@'],
  )
endforeach
# Only its headers and library: its CMake package also hands on Qt's own
# release flags (-DQT_NO_DEBUG), which turned Qt's checks (bounds and the
# like) off in just the programs that use it (the Sniffer, the Collar, Pack)
# and made their copies of shared code differ from every other program's.
layershellqt = dependency('LayerShellQt', method: 'cmake',
  modules: ['LayerShellQt::Interface']).partial_dependency(includes: true, links: true, link_args: true)
# GIO reads and launches XDG desktop entries for the Applications folder,
# so the desktop entry specification is followed rather than approximated.
# With it, Limited accounts' program filter (lib/appfilter.c): every
# program that lists or launches applications through GIO uses both.
gio_unix = [dependency('gio-unix-2.0'), appfilter_dep]


# libzacos9-shell first: panelkit runs helpers through it (sharingclient.cpp).
subdir('common')
# Control panels (the Finder's Get Info uses their controls too).
panelkit = static_library('panelkit', 'panels/panelkit.cpp', 'panels/sharingclient.cpp',
  'panels/custompatterns.cpp',
  'panels/customthemes.cpp',
  dependencies: [shell_common_dep, platinum_dep, qt6],
)
xkb_layouts_include = include_directories('../compositor/src')

# vptr: scripted pointer (press/drag/release) for UI tests.
fullscreen_client = executable('test-fullscreen-client',
  '../tests/compositor/fullscreen_client.cpp',
  dependencies: qt6,
)

vptr_protocol = []
foreach kind : [['client-header', '@BASENAME@-client-protocol.h'], ['private-code', '@BASENAME@-protocol.c']]
  vptr_protocol += custom_target('vptr_' + kind[0].underscorify(),
    input: meson.project_source_root() / 'protocols/wlr-virtual-pointer-unstable-v1.xml',
    output: kind[1],
    command: [wayland_scanner, kind[0], '@INPUT@', '@OUTPUT@'],
  )
endforeach
vptr_exe = executable('vptr', meson.project_source_root() / 'tools/vptr/vptr.c', vptr_protocol,
  client_protocol_src,
  dependencies: [wayland_client, xkbcommon_client])

# One screen's exact pixels for UI tests (grim resamples at 1.5x and 1.75x).
rawshot_exe = executable('rawshot', meson.project_source_root() / 'tools/rawshot/rawshot.c',
  snapshot_protocol,
  dependencies: [wayland_client])

# Screen saver, display and system sleep (compositor/src/idle.c).
idle_client = executable('test-idle-client',
  '../tests/compositor/idle_client.cpp',
  dependencies: qt6,
)
# Monitors' 1.25x, 1.5x and 1.75x, per screen: wp_fractional_scale_v1, but not for ZacOS's own programs.
globals_client = executable('test-globals-client',
  '../tests/compositor/globals_client.c',
  dependencies: [wayland_client],
)
# Option-click on the close and collapse boxes, ⌘-drag behind (input.c).
close_app_client = executable('test-close-app-client',
  '../tests/compositor/close_app_client.c', finder_protocol,
  dependencies: [wayland_client],
)
gtk3 = dependency('gtk+-3.0')
qt_menu_fixture = executable('qt-menu-fixture', '../tests/finder/qt_menu_fixture.cpp',
  dependencies: qt6)
subdir('menubar')
subdir('classic')
subdir('wine')
subdir('lock')
subdir('screensaver')
subdir('keyboard')
subdir('inhibit')
subdir('snapshot')
subdir('files')
subdir('autostart')
subdir('fileportal')
subdir('panels')
subdir('finderctl')
subdir('disksetup')
subdir('walkies')
subdir('jigsaw')
subdir('mousepractice')
subdir('plotter')
subdir('retriever')
subdir('screenshare')
subdir('network')
subdir('greeter')
subdir('pack')
subdir('scribble')
subdir('dogears')
subdir('notepad')
subdir('stash')
subdir('barkrecorder')
subdir('print')
subdir('help')
subdir('pedigree')
subdir('calculator')
subdir('picture')
subdir('photobooth')
subdir('store')
subdir('update')
subdir('keytags')
subdir('collar')
subdir('installer')
subdir('appinstall')
subdir('wininstall')
subdir('setup')
subdir('qtstyle')
subdir('finder')
subdir('gtkmenu')
subdir('themeinstaller')
test('fullscreen-protocols', find_program('python3'),
  args: [files('../tests/compositor/test_fullscreen.py'),
    meson.project_build_root() / 'compositor/zacos9-wm', fullscreen_client],
  depends: compositor_exe,
  timeout: 30,
)
test('fullscreen-protocols-hidpi', find_program('python3'),
  args: [files('../tests/compositor/test_fullscreen.py'),
    meson.project_build_root() / 'compositor/zacos9-wm', fullscreen_client, '2'],
  depends: compositor_exe,
  timeout: 30,
)
# The same, saying where its focus is as ZacOS's own programs do.
field_client = executable('test-field-client',
  '../tests/compositor/idle_client.cpp',
  include_directories: include_directories('finder'),
  dependencies: [shell_common_dep, qt6, wayland_client],
  cpp_args: ['-DPLATINUM_FIELD'],
)
# The on-screen keyboard for a text field with no keyboard attached.
test('onscreen-keyboard', find_program('python3'),
  args: [files('../tests/compositor/test_onscreen_keyboard.py'), compositor_exe, idle_client, field_client,
    vptr_exe, rawshot_exe],
  depends: [keyboard_exe],
  timeout: 120,
)
test('idle-sleep', find_program('python3'),
  args: [files('../tests/compositor/test_idle.py'), compositor_exe, idle_client, vptr_exe],
  timeout: 60,
)
test('idle-lock', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(), files('../tests/compositor/test_lock.py'),
    compositor_exe, idle_client, test_lock_exe, inhibit_exe, vptr_exe],
  timeout: 90,
)
# The lock at 1.5x and 1.75x: the screen's own pixels, only its 1x colours.
test('lock-scale', find_program('python3'),
  args: [files('../tests/compositor/test_lock_scale.py'), compositor_exe, test_lock_exe, vptr_exe,
    rawshot_exe],
  timeout: 90,
)
# ⌘-Tab, Option-click on the desktop, contextual menus and Log Out, with the
# real menu bar and Sniffer.
test('session-ui', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(), files('../tests/compositor/test_session_ui.py'),
    compositor_exe, menubar_exe, finder_exe, idle_client, vptr_exe],
  timeout: 90,
)
# The Pack: torn off the Application menu (menubar/main.c, pack/).
test('pack-session', find_program('python3'),
  args: [files('../tests/compositor/test_pack.py'), compositor_exe, menubar_exe, idle_client, vptr_exe],
  depends: [pack_exe],
  timeout: 60,
)
# Function keys that open things (fkeys.c, input.c).
test('function-keys-session', find_program('python3'),
  args: [files('../tests/compositor/test_function_keys.py'), compositor_exe, idle_client, vptr_exe],
  timeout: 60,
)
# Sticky Keys, Mouse Keys and Zoom (access.c, input.c, output.c).
test('accessibility-session', find_program('python3'),
  args: [files('../tests/compositor/test_access.py'), compositor_exe, idle_client, vptr_exe],
  timeout: 90,
)
# Proxy icons and spring-open collapsed windows (decor.c, input.c, folderwindow.cpp).
test('proxy-icon', find_program('python3'),
  args: [files('../tests/compositor/test_proxy_icon.py'), compositor_exe, menubar_exe, finder_exe, vptr_exe],
  timeout: 90,
)
# Pop-up windows: tabs at the bottom of the screen (decor.c, view.c, input.c, folderwindow.cpp).
test('popup-windows', find_program('python3'),
  args: [files('../tests/compositor/test_popup_windows.py'), compositor_exe, menubar_exe, finder_exe, vptr_exe],
  timeout: 120,
)
# Mouse Tracks: a trail behind the moving pointer (compositor/src/tracks.c).
test('mouse-tracks', find_program('python3'),
  args: [files('../tests/compositor/test_mouse_tracks.py'), compositor_exe, vptr_exe,
    scribble_exe],
  timeout: 60,
)
# Spring-loaded folders: Space during a drag opens one at once (drag_spring).
test('spring-space', find_program('python3'),
  args: [files('../tests/compositor/test_spring_space.py'), compositor_exe, menubar_exe, finder_exe, vptr_exe],
  timeout: 120,
)
# The Qt programs at 1.25x, 1.5x and 1.75x: a panel exactly its 1x picture,
# each pixel whole screen pixels.
test('panel-scale', find_program('python3'),
  args: [files('../tests/compositor/test_panel_scale.py'), compositor_exe, controlpanel_exe,
    rawshot_exe],
  timeout: 120,
)
# Window frames at 1.25x, 1.5x and 1.75x: the screen's own pixels, even stripes.
test('frame-scale', find_program('python3'),
  args: [files('../tests/compositor/test_frame_scale.py'), compositor_exe, idle_client, vptr_exe,
    rawshot_exe],
  timeout: 120,
)
# The menu bar at 1.25x, 1.5x and 1.75x: the 1x one, each pixel whole screen pixels.
test('menubar-scale', find_program('python3'),
  args: [files('../tests/compositor/test_menubar_scale.py'), compositor_exe, menubar_exe, vptr_exe,
    rawshot_exe],
  timeout: 120,
)
# The menu bar on every screen, each at its own scale; menus open where clicked.
test('menubar-displays', find_program('python3'),
  args: [files('../tests/compositor/test_menubar_displays.py'), compositor_exe, menubar_exe, vptr_exe],
  timeout: 90,
)
test('fractional-scale', find_program('python3'),
  args: [files('../tests/compositor/test_fractional_scale.py'), compositor_exe, globals_client],
  timeout: 60,
)
# Keyboard layouts: ⌘-Space and the keyboard menu (input.c, kbmenu.c).
test('keyboard-layout-switch', find_program('python3'),
  args: [files('../tests/compositor/test_keyboard_layouts.py'), compositor_exe, menubar_exe, vptr_exe],
  timeout: 60,
)
test('window-tricks', find_program('python3'),
  args: [files('../tests/compositor/test_window_tricks.py'), compositor_exe, idle_client,
    vptr_exe, close_app_client],
  timeout: 60,
)
# A keyring password prompt keeps the keyboard until answered (compositor/src/view.c).
test('system-prompt', find_program('python3'),
  args: [files('../tests/compositor/test_system_prompt.py'), compositor_exe, idle_client,
    vptr_exe],
  timeout: 60,
)
test('idle-inhibit', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(), files('../tests/compositor/test_inhibit.py'),
    compositor_exe, idle_client, inhibit_exe],
  timeout: 60,
)

busy_client = executable('test-busy-client',
  '../tests/compositor/busy_client.cpp',
  include_directories: include_directories('finder'),
  dependencies: [shell_common_dep, qt6, wayland_client],
)
test('busy-cursor-protocols', find_program('python3'),
  args: [files('../tests/compositor/test_busy_cursor.py'), compositor_exe, busy_client, vptr_exe,
    meson.project_source_root() / 'tools/cursors/build_cursors.py',
    meson.project_source_root() / 'assets/cursors/platinum-cursors.pcur'],
  depends: [compositor_exe, cursor_theme],
  timeout: 45,
)
test('ghost-frames', find_program('python3'),
  args: [files('../tests/compositor/test_ghost_frames.py'), compositor_exe, busy_client],
  depends: [compositor_exe],
  timeout: 60,
)
test('gtk-save-places', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(),
    files('../tests/finder/test_desktop_gtk.py'), compositor_exe.full_path(),
    files('../session/zacos9-desktop-places'), gtk_appmenu_module.full_path()],
  depends: [gtk_appmenu_module, compositor_exe],
  timeout: 30,
)
test('gtk-theme-css',
  executable('test-gtk-theme', '../tests/finder/test_gtk_theme.c',
    dependencies: [gtk3]),
  args: [meson.project_source_root() / 'share/themes/ZacOS9/gtk-3.0/gtk.css'],
)
test('gtk-menu-protocol', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(),
    files('../tests/finder/test_gtk_menu_protocol.py'),
    compositor_exe.full_path(), gtk_menu_observer.full_path(),
    files('../tests/finder/gtk_menu_fixture.py'), gtk_appmenu_module.full_path(),
    qt_menu_fixture.full_path()],
  depends: [compositor_exe, gtk_menu_observer, gtk_appmenu_module, qt_menu_fixture],
  timeout: 45,
)

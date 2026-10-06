wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor')
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

# Expanding archives on a double-click (the Finder runs it from beside itself).
configure_file(input: 'finder/zacos9-expand', output: 'zacos9-expand', copy: true)
install_data('finder/zacos9-expand', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
test('expand', find_program('python3'),
  args: [files('../tests/finder/test_expand.py'), files('finder/zacos9-expand')])

# Screen Snapshot (Apple menu): drag out a rectangle; a picture on the
# Desktop and the clipboard (slurp, native committed-frame capture, wl-copy).
configure_file(input: 'snapshot/zacos9-snapshot', output: 'zacos9-snapshot', copy: true)
install_data('snapshot/zacos9-snapshot', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')

# The classic Mac OS launcher, next to the Finder that runs it.
configure_file(input: 'classic/zacos9-classic', output: 'zacos9-classic', copy: true)
install_data('classic/zacos9-classic', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')

# The Wine launcher for Windows applications, next to the Finder.
configure_file(input: 'wine/zacos9-wine', output: 'zacos9-wine', copy: true)
install_data('wine/zacos9-wine', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')

# GDBus: the menu bar's global menus (registrar, dbusmenu); GIO for the
# Finder's desktop entries too.
gio = dependency('gio-2.0')

test('erase-disk-menu',
  executable('test-erase-menu', 'menubar/test_erase_menu.c',
    dependencies: [platinum_dep, wayland_client],
    c_args: ['-ffunction-sections', '-fdata-sections'],
    link_args: ['-Wl,--gc-sections']))

# platinum-shell-v1: the menu bar (front window's pid) and the Finder.
finder_protocol = []
foreach kind : [['client-header', '@BASENAME@-client-protocol.h'], ['private-code', '@BASENAME@-protocol.c']]
  finder_protocol += custom_target('finder_platinum_shell_' + kind[0].underscorify(),
    input: meson.project_source_root() / 'protocols/platinum-shell-v1.xml',
    output: kind[1],
    command: [wayland_scanner, kind[0], '@INPUT@', '@OUTPUT@'],
  )
endforeach

executable('zacos9-menubar',
  'menubar/main.c',
  'menubar/menus.c',
  'menubar/toplevels.c',
  'menubar/launch.c',
  'menubar/keys.c',
  'menubar/finderlink.c',
  'menubar/appmenu.c',
  client_protocol_src,
  finder_protocol,
  dependencies: [platinum_dep, wayland_client, wayland_cursor, xkbcommon_client, gio],
  install: true,
)

test('menu-launch-feedback',
  executable('test-menu-launch',
    'menubar/test_launch.c', 'menubar/launch.c',
    dependencies: [platinum_dep, wayland_client, gio],
  ),
  timeout: 10,
)

# The Finder: Qt6 for windows, input and files; all drawing through lib/.
add_languages('cpp', native: false)
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
capture_exe = executable('zacos9-capture', 'snapshot/capture.cpp',
  'finder/alert.cpp', 'finder/platinumshell.cpp', finder_protocol, snapshot_protocol,
  dependencies: [platinum_dep, qt6, wayland_client],
  include_directories: include_directories('finder'),
  cpp_args: ['-std=c++17'],
  install: true,
)
test('snapshot-image',
  executable('test-snapshot-image', '../tests/finder/test_snapshot_image.cpp',
    'finder/alert.cpp', 'finder/platinumshell.cpp', finder_protocol, snapshot_protocol,
    dependencies: [platinum_dep, qt6, wayland_client],
    include_directories: include_directories('finder'),
    cpp_args: ['-std=c++17']),
  env: ['QT_QPA_PLATFORM=offscreen'],
)
snapshot_animation = executable('test-snapshot-animation',
  '../tests/finder/snapshot_animation.cpp',
  dependencies: [qt6], cpp_args: ['-std=c++17'])
test('native-snapshot', find_program('python3'),
  args: [files('../tests/finder/test_snapshot.py'), compositor_exe, capture_exe,
    files('snapshot/zacos9-snapshot'), snapshot_animation],
  timeout: 60,
)
layershellqt = dependency('LayerShellQt', method: 'cmake',
  modules: ['LayerShellQt::Interface'])
# GIO reads and launches XDG desktop entries for the Applications folder,
# so the desktop entry specification is followed rather than approximated.
gio_unix = dependency('gio-unix-2.0')

# Control panels (the Finder's Get Info uses their controls too).
panelkit = static_library('panelkit', 'panels/panelkit.cpp', 'panels/sharingclient.cpp',
  'panels/custompatterns.cpp',
  'panels/customthemes.cpp',
  include_directories: include_directories('finder'),
  dependencies: [platinum_dep, qt6],
  cpp_args: ['-std=c++17'],
)

configure_file(input: 'panels/zacos9-patterns', output: 'zacos9-patterns', copy: true)
configure_file(input: 'panels/zacos9-soundthemes', output: 'zacos9-soundthemes', copy: true)
install_data('panels/zacos9-soundthemes', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
test('sound-themes', find_program('python3'),
  args: [files('../tests/panels/test_soundthemes.py'), files('panels/zacos9-soundthemes')],
  timeout: 60,
)
test('custom-themes',
  executable('test-customthemes', '../tests/panels/test_customthemes.cpp', 'panels/appearance.cpp',
    link_with: panelkit,
    include_directories: include_directories('panels', 'finder'),
    dependencies: [platinum_dep, qt6],
    cpp_args: ['-std=c++17'],
  ),
  args: [meson.project_source_root() / 'assets/sounds/woodblock.wav'],
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)
install_data('panels/zacos9-patterns', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
test('custom-patterns', find_program('python3'),
  args: [files('../tests/panels/test_patterns.py'), files('panels/zacos9-patterns')],
  timeout: 60,
)
test('custom-pattern-catalog',
  executable('test-custompatterns', '../tests/panels/test_custompatterns.cpp', 'panels/appearance.cpp',
    link_with: panelkit,
    include_directories: include_directories('panels', 'finder'),
    dependencies: [platinum_dep, qt6],
    cpp_args: ['-std=c++17'],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 30,
)

executable('zacos9-finder',
  'finder/main.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  'finder/desktop.cpp',
  'finder/secondarydesktop.cpp',
  'finder/folderwindow.cpp',
  'finder/items.cpp',
  'finder/finder.cpp',
  'finder/fileops.cpp',
  'finder/transferdialog.cpp',
  'finder/labeleditor.cpp',
  'finder/infowindow.cpp',
  'finder/findwindow.cpp',
  'finder/folderstate.cpp',
  'finder/vfs.cpp',
  'finder/appdb.cpp',
  'finder/localvolumes.cpp',
  'finder/diskinit.cpp',
  'finder/filemanager1.cpp',
  'network/netvolumes.cpp',
  'network/smbclient.cpp',
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels', 'network'),
  dependencies: [platinum_dep, qt6, layershellqt, wayland_client, gio, gio_unix],
  cpp_args: ['-std=c++17', '-DZACOS9_DISK_HELPER="@0@"'.format(
    get_option('prefix') / get_option('libexecdir') / 'zacos9' / 'zacos9-disk-helper')],
  install: true,
)

# The Macintosh view of this computer: the curated hierarchy, and the
# Applications folder built from XDG desktop entries. The test builds a
# private XDG environment, so it reads only its own desktop entries.
finder_test_sources = [
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  'finder/desktop.cpp',
  'finder/secondarydesktop.cpp',
  'finder/folderwindow.cpp',
  'finder/items.cpp',
  'finder/finder.cpp',
  'finder/fileops.cpp',
  'finder/transferdialog.cpp',
  'finder/labeleditor.cpp',
  'finder/infowindow.cpp',
  'finder/findwindow.cpp',
  'finder/folderstate.cpp',
  'finder/vfs.cpp',
  'finder/appdb.cpp',
  'finder/diskinit.cpp',
  'finder/filemanager1.cpp',
  'network/netvolumes.cpp',
  'network/smbclient.cpp',
]
test('finder-selection',
  executable('test-finder-selection', '../tests/finder/test_selection.cpp', finder_test_sources,
    'finder/localvolumes.cpp',
    link_with: panelkit,
    include_directories: include_directories('finder', 'panels', 'network'),
    dependencies: [platinum_dep, qt6, layershellqt, wayland_client, gio, gio_unix],
    cpp_args: ['-std=c++17'],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)
test('finder-vfs',
  executable('test-finder-vfs',
    'finder/tests/test_vfs.cpp',
    'finder/vfs.cpp',
    'finder/appdb.cpp',
    'finder/platinumshell.cpp',
    finder_protocol,
    'finder/items.cpp',
    link_with: panelkit,
    include_directories: include_directories('finder', 'panels'),
    dependencies: [platinum_dep, qt6, gio, gio_unix, wayland_client],
    cpp_args: ['-std=c++17'],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 180,
)

test('disk-initialization-ui',
  executable('test-diskinit', '../tests/finder/test_diskinit.cpp',
    'finder/diskinit.cpp', 'finder/localvolumes.cpp',
    'finder/alert.cpp', 'finder/platinumshell.cpp', finder_protocol,
    dependencies: [platinum_dep, qt6, gio, wayland_client],
    include_directories: include_directories('finder', 'network', 'panels'),
    link_with: panelkit,
    cpp_args: ['-std=c++17'],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)

test('local-volume-hotplug',
  executable('test-localvolumes', '../tests/finder/test_localvolumes.cpp',
    'finder/localvolumes.cpp',
    dependencies: [qt6, gio],
    include_directories: include_directories('finder', 'network'),
    cpp_args: ['-std=c++17'],
  ),
  env: ['QT_NO_GLIB=1'],
  timeout: 10,
)

executable('zacos9-appearance',
  'panels/main.cpp',
  'panels/appearance.cpp',
  link_with: panelkit,
  include_directories: include_directories('finder'), # pixels.h
  dependencies: [platinum_dep, qt6],
  cpp_args: ['-std=c++17'],
  install: true,
)

executable('zacos9-datetime',
  'panels/datetime_main.cpp',
  'panels/datetime.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder'),
  dependencies: [platinum_dep, qt6, wayland_client],
  cpp_args: ['-std=c++17'],
  install: true,
)

executable('zacos9-tcpip',
  'panels/tcpip.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder'),
  dependencies: [platinum_dep, qt6, wayland_client],
  cpp_args: ['-std=c++17'],
  install: true,
)

# Bluetooth: BlueZ over the system D-Bus; headsets and speakers become the
# sound output (pactl).
executable('zacos9-bluetooth',
  'panels/bluetooth.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder'),
  dependencies: [platinum_dep, qt6, wayland_client],
  cpp_args: ['-std=c++17'],
  install: true,
)

executable('zacos9-filesharing',
  'panels/filesharing.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder'),
  dependencies: [platinum_dep, qt6, wayland_client],
  cpp_args: ['-std=c++17'],
  install: true,
)

# Parsing avahi-browse and WS-Discovery replies, against recorded text
# (see the test file for what could and couldn't be checked live).
test('discovery',
  executable('test-discovery',
    'network/tests/test_discovery.cpp',
    'network/discovery.cpp',
    include_directories: include_directories('network'),
    dependencies: [platinum_dep, qt6],
    cpp_args: ['-std=c++17'],
  ),
  timeout: 60,
)

executable('zacos9-netbrowser',
  'network/main.cpp',
  'network/netbrowser.cpp',
  'network/connectdialog.cpp',
  'network/logindialog.cpp',
  'network/volumedialog.cpp',
  'network/discovery.cpp',
  'network/afpclient.cpp',
  'network/smbclient.cpp',
  'network/netvolumes.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels', 'network'),
  dependencies: [platinum_dep, qt6, wayland_client],
  cpp_args: ['-std=c++17'],
  install: true,
)

executable('zacos9-store',
  'store/main.cpp',
  'store/store.cpp',
  'store/storeclient.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  'finder/appdb.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels'),
  dependencies: [platinum_dep, qt6, wayland_client, gio, gio_unix],
  cpp_args: ['-std=c++17'],
  install: true,
)

test('store-browser',
  executable('test-store-browser',
    '../tests/store/test_browser.cpp',
    'store/store.cpp', 'store/storeclient.cpp',
    'finder/alert.cpp', 'finder/platinumshell.cpp', 'finder/appdb.cpp',
    finder_protocol,
    link_with: panelkit,
    include_directories: include_directories('finder', 'panels', 'store'),
    dependencies: [platinum_dep, qt6, wayland_client, gio, gio_unix],
    cpp_args: ['-std=c++17'],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)

# The Software window's catalog: reading it, filtering, and the helper
# script's own package-name validation, run directly (never through
# pkexec, and never with a package name that could really install or
# remove anything).
# Software Update's checks: reading a GitHub release and apt's simulation,
# and Debian version order (dpkg's own); no network, nothing installed.
test('update-check',
  executable('test-update-check',
    'update/tests/test_updatecheck.cpp',
    'update/updatecheck.cpp',
    include_directories: include_directories('update'),
    dependencies: [qt6],
    cpp_args: ['-std=c++17'],
  ),
  timeout: 60,
)

test('store-client',
  executable('test-store-client',
    'store/tests/test_storeclient.cpp',
    'store/storeclient.cpp',
    include_directories: include_directories('finder', 'store'), # settings.h, storeclient.h
    dependencies: [platinum_dep, qt6],
    cpp_args: ['-std=c++17'],
  ),
  timeout: 60,
)

executable('zacos9-controlpanel',
  'panels/controlpanel.cpp',
  'panels/soundclient.cpp',
  link_with: panelkit,
  include_directories: include_directories('finder'),
  dependencies: [platinum_dep, qt6],
  cpp_args: ['-std=c++17'],
  install: true,
)

test('sound-output',
  executable('test-sound-output',
    'panels/tests/test_sound.cpp',
    'panels/soundclient.cpp',
    include_directories: include_directories('panels'),
    dependencies: qt6,
    cpp_args: ['-std=c++17'],
  ),
  args: files('panels/tests/pactl-fixture.py'),
  timeout: 60,
)

executable('zacos9-installer',
  'installer/main.cpp',
  'installer/installer.cpp',
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels'),
  dependencies: [platinum_dep, qt6],
  cpp_args: ['-std=c++17'],
  install: true,
)

executable('zacos9-appinstall',
  'appinstall/main.cpp',
  'appinstall/appinstall.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels'),
  dependencies: [platinum_dep, qt6, wayland_client],
  cpp_args: ['-std=c++17'],
  install: true,
)

# Software Update (Apple menu): ZacOS 9 releases from GitHub, Debian's
# updates through apt; installs through zacos9-appstore-helper.
executable('zacos9-update',
  'update/main.cpp',
  'update/update.cpp',
  'update/updatecheck.cpp',
  'store/storeclient.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels', 'store'),
  dependencies: [platinum_dep, qt6, wayland_client],
  cpp_args: ['-std=c++17'],
  install: true,
)

executable('zacos9-wininstall',
  'wininstall/main.cpp',
  'wininstall/wininstall.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels'),
  dependencies: [platinum_dep, qt6, wayland_client],
  cpp_args: ['-std=c++17'],
  install: true,
)

executable('zacos9-setup',
  'setup/main.cpp',
  'setup/setup.cpp',
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels'),
  dependencies: [platinum_dep, qt6],
  cpp_args: ['-std=c++17'],
  install: true,
)

# vptr: scripted pointer (press/drag/release) for UI tests.
fullscreen_client = executable('test-fullscreen-client',
  '../compositor/tests/fullscreen_client.cpp',
  dependencies: qt6,
  cpp_args: ['-std=c++17'],
)
test('fullscreen-protocols', find_program('python3'),
  args: [files('../compositor/tests/test_fullscreen.py'),
    meson.project_build_root() / 'compositor/zacos9-wm', fullscreen_client],
  depends: compositor_exe,
  timeout: 30,
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

busy_client = executable('test-busy-client',
  '../compositor/tests/busy_client.cpp', 'finder/platinumshell.cpp', finder_protocol,
  include_directories: include_directories('finder'),
  dependencies: [qt6, wayland_client],
  cpp_args: ['-std=c++17'],
)
test('busy-cursor-protocols', find_program('python3'),
  args: [files('../compositor/tests/test_busy_cursor.py'), compositor_exe, busy_client, vptr_exe,
    meson.project_source_root() / 'tools/cursors/build_cursors.py',
    meson.project_source_root() / 'assets/cursors/platinum-cursors.pcur'],
  depends: [compositor_exe, cursor_theme],
  timeout: 45,
)

# ZacOS 9 Qt style plugin: other Qt applications (Qt 6) drawn with the
# Platinum controls. Selected with QT_STYLE_OVERRIDE=zacos9 (session/).
qt6_mod = import('qt6')
qtstyle_moc = qt6_mod.preprocess(moc_headers: 'qtstyle/zacos9style.h',
  dependencies: qt6)
shared_module('zacos9style',
  'qtstyle/zacos9style.cpp', qtstyle_moc,
  include_directories: include_directories('qtstyle'),
  dependencies: [platinum_dep, qt6],
  cpp_args: ['-std=c++17'],
  name_prefix: '',
  install: true,
  install_dir: get_option('libdir') / 'qt6' / 'plugins' / 'styles',
)
gtk_menu_observer = executable('gtk-menu-observer',
  '../tests/finder/gtk_menu_observer.c', finder_protocol, 'menubar/appmenu.c',
  include_directories: include_directories('menubar'),
  dependencies: [platinum_dep, wayland_client, gio],
)
test('gtk-appmenu', find_program('dbus-run-session'),
  args: [executable('test-appmenu-gtk', 'menubar/test_appmenu_gtk.c',
    dependencies: [platinum_dep, wayland_client, gio])],
  env: ['G_DEBUG=fatal-warnings'], timeout: 15)
gtk3 = dependency('gtk+-3.0')
gtk_parser = dependency('appmenu-gtk3-parser')
gtk_appmenu_module = shared_module('zacos9-appmenu',
  'gtkmenu/module.c',
  dependencies: [gtk3, gtk_parser],
  install: true,
  install_dir: get_option('libdir') / 'gtk-3.0' / 'modules',
)
qt_menu_fixture = executable('qt-menu-fixture', '../tests/finder/qt_menu_fixture.cpp',
  dependencies: qt6)
test('gtk-menu-protocol', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(),
    files('../tests/finder/test_gtk_menu_protocol.py'),
    compositor_exe.full_path(), gtk_menu_observer.full_path(),
    files('../tests/finder/gtk_menu_fixture.py'), gtk_appmenu_module.full_path(),
    qt_menu_fixture.full_path()],
  depends: [compositor_exe, gtk_menu_observer, gtk_appmenu_module, qt_menu_fixture],
  timeout: 45,
)
test('finder-desktop',
  executable('test-finder-desktop',
    '../tests/finder/test_desktop.cpp', finder_test_sources,
    'qtstyle/zacos9style.cpp', qtstyle_moc,
    link_with: panelkit,
    include_directories: include_directories('finder', 'panels', 'network', 'qtstyle'),
    dependencies: [platinum_dep, qt6, layershellqt, wayland_client, gio, gio_unix],
    cpp_args: ['-std=c++17'],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)

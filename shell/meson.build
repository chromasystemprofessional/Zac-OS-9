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

# The Finder: Qt6 for windows, input and files; all drawing through lib/.
add_languages('cpp', native: false)
qt6 = dependency('qt6', modules: ['Core', 'Gui', 'Widgets', 'Network'],
  private_headers: true) # QPlatformNativeInterface: a window's wl_surface
layershellqt = dependency('LayerShellQt', method: 'cmake',
  modules: ['LayerShellQt::Interface'])
# GIO reads and launches XDG desktop entries for the Applications folder,
# so the desktop entry specification is followed rather than approximated.
gio_unix = dependency('gio-unix-2.0')

# Control panels (the Finder's Get Info uses their controls too).
panelkit = static_library('panelkit', 'panels/panelkit.cpp', 'panels/sharingclient.cpp',
  include_directories: include_directories('finder'),
  dependencies: [platinum_dep, qt6],
  cpp_args: ['-std=c++17'],
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
  'finder/labeleditor.cpp',
  'finder/infowindow.cpp',
  'finder/findwindow.cpp',
  'finder/folderstate.cpp',
  'finder/vfs.cpp',
  'finder/appdb.cpp',
  'finder/localvolumes.cpp',
  'network/netvolumes.cpp',
  'network/smbclient.cpp',
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels', 'network'),
  dependencies: [platinum_dep, qt6, layershellqt, wayland_client, gio, gio_unix],
  cpp_args: ['-std=c++17'],
  install: true,
)

# The Macintosh view of this computer: the curated hierarchy, and the
# Applications folder built from XDG desktop entries. The test builds a
# private XDG environment, so it reads only its own desktop entries.
test('finder-vfs',
  executable('test-finder-vfs',
    'finder/tests/test_vfs.cpp',
    'finder/vfs.cpp',
    'finder/appdb.cpp',
    'finder/items.cpp',
    link_with: panelkit,
    include_directories: include_directories('finder', 'panels'),
    dependencies: [platinum_dep, qt6, gio, gio_unix],
    cpp_args: ['-std=c++17'],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 180,
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
  link_with: panelkit,
  include_directories: include_directories('finder'),
  dependencies: [platinum_dep, qt6],
  cpp_args: ['-std=c++17'],
  install: true,
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
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels'),
  dependencies: [platinum_dep, qt6],
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
vptr_protocol = []
foreach kind : [['client-header', '@BASENAME@-client-protocol.h'], ['private-code', '@BASENAME@-protocol.c']]
  vptr_protocol += custom_target('vptr_' + kind[0].underscorify(),
    input: meson.project_source_root() / 'protocols/wlr-virtual-pointer-unstable-v1.xml',
    output: kind[1],
    command: [wayland_scanner, kind[0], '@INPUT@', '@OUTPUT@'],
  )
endforeach
executable('vptr', meson.project_source_root() / 'tools/vptr/vptr.c', vptr_protocol,
  client_protocol_src,
  dependencies: [wayland_client, xkbcommon_client])

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

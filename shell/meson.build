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
configure_file(input: 'classic/platinum-classic', output: 'platinum-classic', copy: true)
install_data('classic/platinum-classic', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')

executable('platinum-menubar',
  'menubar/main.c',
  'menubar/menus.c',
  'menubar/toplevels.c',
  'menubar/launch.c',
  'menubar/keys.c',
  'menubar/finderlink.c',
  client_protocol_src,
  dependencies: [platinum_dep, wayland_client, wayland_cursor, xkbcommon_client],
  install: true,
)

# The Finder: Qt6 for windows, input and files; all drawing through lib/.
add_languages('cpp', native: false)
qt6 = dependency('qt6', modules: ['Core', 'Gui', 'Widgets', 'Network'],
  private_headers: true) # QPlatformNativeInterface: a window's wl_surface
layershellqt = dependency('LayerShellQt', method: 'cmake',
  modules: ['LayerShellQt::Interface'])

finder_protocol = []
foreach kind : [['client-header', '@BASENAME@-client-protocol.h'], ['private-code', '@BASENAME@-protocol.c']]
  finder_protocol += custom_target('finder_platinum_shell_' + kind[0].underscorify(),
    input: meson.project_source_root() / 'protocols/platinum-shell-v1.xml',
    output: kind[1],
    command: [wayland_scanner, kind[0], '@INPUT@', '@OUTPUT@'],
  )
endforeach

executable('platinum-finder',
  'finder/main.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  'finder/desktop.cpp',
  'finder/folderwindow.cpp',
  'finder/items.cpp',
  'finder/finder.cpp',
  'finder/fileops.cpp',
  'finder/labeleditor.cpp',
  'finder/infowindow.cpp',
  'finder/findwindow.cpp',
  'finder/folderstate.cpp',
  dependencies: [platinum_dep, qt6, layershellqt, wayland_client],
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

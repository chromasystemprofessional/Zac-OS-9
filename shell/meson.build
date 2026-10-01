wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor')
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

executable('platinum-menubar',
  'menubar/main.c',
  'menubar/menus.c',
  'menubar/toplevels.c',
  'menubar/launch.c',
  'menubar/logo.c',
  client_protocol_src,
  dependencies: [platinum_dep, wayland_client, wayland_cursor],
  install: true,
)

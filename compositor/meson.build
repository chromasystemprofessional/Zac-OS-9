wayland_server = dependency('wayland-server')
wayland_protos = dependency('wayland-protocols')
wlroots = dependency('wlroots-0.18')
xkbcommon = dependency('xkbcommon')

wayland_scanner = find_program(
  dependency('wayland-scanner', native: true).get_variable('wayland_scanner'),
  native: true,
)
wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')

xdg_shell_xml = wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml'
xdg_shell_h = custom_target('xdg-shell-protocol.h',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

executable('platinum-wm',
  'src/main.c',
  'src/output.c',
  'src/input.c',
  'src/view.c',
  xdg_shell_h,
  dependencies: [wayland_server, wlroots, xkbcommon],
  install: true,
)

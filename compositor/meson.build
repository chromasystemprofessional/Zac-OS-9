wayland_server = dependency('wayland-server')
wayland_protos = dependency('wayland-protocols')
wlroots = dependency('wlroots-0.18')
xkbcommon = dependency('xkbcommon')
cairo = dependency('cairo')
xcb_icccm = dependency('xcb-icccm').partial_dependency(compile_args: true, includes: true)
libdrm = dependency('libdrm').partial_dependency(compile_args: true, includes: true)

wayland_scanner = find_program(
  dependency('wayland-scanner', native: true).get_variable('wayland_scanner'),
  native: true,
)
wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')

protocols = [
  wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
  wl_protocol_dir / 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml',
]
protocol_headers = []
foreach xml : protocols
  protocol_headers += custom_target(xml.underscorify() + '_h',
    input: xml,
    output: '@BASENAME@-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  )
endforeach

# Pure drawing code, shared with the unit tests.
decor_lib = static_library('platinum-decor',
  'src/decor.c',
  'src/text.c',
  dependencies: [cairo],
)

executable('platinum-wm',
  'src/main.c',
  'src/output.c',
  'src/input.c',
  'src/view.c',
  'src/xdg.c',
  'src/xwayland.c',
  'src/frame.c',
  'src/outline.c',
  'src/pixbuf.c',
  protocol_headers,
  link_with: decor_lib,
  dependencies: [wayland_server, wlroots, xkbcommon, cairo, libdrm, xcb_icccm],
  install: true,
)

test('decor',
  executable('test-decor',
    'tests/test_decor.c',
    include_directories: include_directories('src'),
    link_with: decor_lib,
    dependencies: [cairo],
  ),
  args: [meson.project_source_root() / 'tools/measure/figs'],
)

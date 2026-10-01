wayland_server = dependency('wayland-server')
wayland_protos = dependency('wayland-protocols')
wlroots = dependency('wlroots-0.18')
xkbcommon = dependency('xkbcommon')
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
  meson.project_source_root() / 'protocols/wlr-layer-shell-unstable-v1.xml',
  meson.project_source_root() / 'protocols/platinum-shell-v1.xml',
]
protocol_headers = []
foreach xml : protocols
  protocol_headers += custom_target(xml.underscorify() + '_h',
    input: xml,
    output: '@BASENAME@-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  )
endforeach
# Our own protocol also needs its interface code on the server side.
protocol_headers += custom_target('platinum_shell_v1_c',
  input: meson.project_source_root() / 'protocols/platinum-shell-v1.xml',
  output: '@BASENAME@-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

# Pure frame-drawing code, shared with the unit tests.
decor_lib = static_library('platinum-decor',
  'src/decor.c',
  dependencies: [platinum_dep],
)
decor_dep = declare_dependency(
  link_with: decor_lib,
  include_directories: include_directories('src'),
  dependencies: [platinum_dep],
)

# The Platinum cursor theme, built next to platinum-wm (build tree) and
# installed with the icon themes.
python3 = find_program('python3')
custom_target('cursor-theme',
  input: meson.project_source_root() / 'assets/cursors/platinum-cursors.pcur',
  output: 'cursors.stamp',
  command: [python3, meson.project_source_root() / 'tools/cursors/build_cursors.py',
    '@INPUT@', meson.current_build_dir()],
  depend_files: meson.project_source_root() / 'tools/cursors/build_cursors.py',
  build_by_default: true,
)
install_subdir(meson.current_build_dir() / 'Platinum',
  install_dir: get_option('datadir') / 'icons')

executable('platinum-wm',
  'src/main.c',
  'src/output.c',
  'src/input.c',
  'src/startup.c',
  'src/prefs.c',
  'src/view.c',
  'src/xdg.c',
  'src/xwayland.c',
  'src/layers.c',
  'src/platinum_shell.c',
  'src/frame.c',
  'src/outline.c',
  'src/pixbuf.c',
  protocol_headers,
  dependencies: [decor_dep, wayland_server, wlroots, xkbcommon, libdrm, xcb_icccm],
  install: true,
)

test('decor',
  executable('test-decor',
    'tests/test_decor.c',
    dependencies: [decor_dep],
  ),
  args: [meson.project_source_root() / 'tools/measure/figs'],
)

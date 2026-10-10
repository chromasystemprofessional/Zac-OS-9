wayland_server = dependency('wayland-server')
wayland_protos = dependency('wayland-protocols')
wlroots = dependency('wlroots-0.18')
xkbcommon = dependency('xkbcommon')
xcb_icccm = dependency('xcb-icccm').partial_dependency(compile_args: true, includes: true)
xcb = dependency('xcb')
libdrm = dependency('libdrm').partial_dependency(compile_args: true, includes: true)

wayland_scanner = find_program(
  dependency('wayland-scanner', native: true).get_variable('wayland_scanner'),
  native: true,
)
wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')

protocols = [
  wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
  wl_protocol_dir / 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml',
  wl_protocol_dir / 'staging/cursor-shape/cursor-shape-v1.xml',
  wl_protocol_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml',
  meson.project_source_root() / 'protocols/wlr-layer-shell-unstable-v1.xml',
  meson.project_source_root() / 'protocols/platinum-shell-v1.xml',
  meson.project_source_root() / 'protocols/zacos-snapshot-v1.xml',
  meson.project_source_root() / 'protocols/gtk-shell.xml',
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
protocol_headers += custom_target('zacos_snapshot_v1_c',
  input: meson.project_source_root() / 'protocols/zacos-snapshot-v1.xml',
  output: '@BASENAME@-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)
# Xwayland's own xdg-output manager (xwayland_output.c).
protocol_headers += custom_target('xdg_output_c',
  input: wl_protocol_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml',
  output: '@BASENAME@-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)
protocol_headers += custom_target('gtk_shell_c',
  input: meson.project_source_root() / 'protocols/gtk-shell.xml',
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

# The ZacOS9 cursor theme, built next to zacos9-wm (build tree) and
# installed with the icon themes.
python3 = find_program('python3')
cursor_theme = custom_target('cursor-theme',
  input: meson.project_source_root() / 'assets/cursors/platinum-cursors.pcur',
  output: 'cursors.stamp',
  command: [python3, meson.project_source_root() / 'tools/cursors/build_cursors.py',
    '@INPUT@', meson.current_build_dir()],
  depend_files: meson.project_source_root() / 'tools/cursors/build_cursors.py',
  build_by_default: true,
)
install_subdir(meson.current_build_dir() / 'ZacOS9',
  install_dir: get_option('datadir') / 'icons')

compositor_exe = executable('zacos9-wm',
  'src/main.c',
  'src/output.c',
  'src/snapshot.c',
  'src/input.c',
  'src/window_sounds.c',
  'src/launch.c',
  'src/startup.c',
  'src/prefs.c',
  'src/hotkey.c',
  'src/view.c',
  'src/xdg.c',
  'src/xwayland.c',
  'src/xwayland_output.c',
  'src/layers.c',
  'src/platinum_shell.c',
  'src/gtk_shell.c',
  'src/frame.c',
  'src/outline.c',
  'src/pixbuf.c',
  protocol_headers,
  dependencies: [decor_dep, wayland_server, wlroots, xkbcommon, libdrm, xcb_icccm, xcb],
  link_depends: cursor_theme,
  install: true,
)

test('decor',
  executable('test-decor',
    'tests/test_decor.c',
    dependencies: [decor_dep],
  ),
  args: [meson.project_source_root() / 'tools/measure/figs'],
)

test('startup-displays',
  executable('test-startup',
    'tests/test_startup.c', 'src/startup.c', 'src/pixbuf.c',
    include_directories: include_directories('src'),
    dependencies: [platinum_dep, wayland_server, wlroots],
  ),
  env: ['ZACOS9_STARTUP=1'],
)

test('cursor-theme', python3,
  args: [files('tests/test_cursors.py'),
    meson.project_source_root() / 'tools/cursors/build_cursors.py',
    meson.project_source_root() / 'assets/cursors/platinum-cursors.pcur'],
)
test('launch-feedback',
  executable('test-launch',
    'tests/test_launch.c', 'src/launch.c',
    include_directories: include_directories('src'),
    dependencies: [platinum_dep, wayland_server, wlroots],
  ),
  timeout: 20,
)
test('virtual-pointer-output',
  executable('test-virtual-pointer',
    'tests/test_virtual_pointer.c',
    protocol_headers,
    include_directories: include_directories('src'),
    dependencies: [decor_dep, wayland_server, wlroots, xkbcommon],
    c_args: ['-ffunction-sections', '-fdata-sections'],
    link_args: ['-Wl,--gc-sections'],
  ),
)

test('window-sounds',
  executable('test-window-sounds',
    'tests/test_window_sounds.c', 'src/window_sounds.c',
    include_directories: include_directories('src', '../lib'),
  ),
)

test('content-inset',
  executable('test-content-inset',
    'tests/test_content_inset.c', 'src/view.c', 'src/frame.c', 'src/pixbuf.c',
    protocol_headers,
    include_directories: include_directories('src'),
    dependencies: [decor_dep, wayland_server, wlroots, libdrm],
    c_args: ['-ffunction-sections', '-fdata-sections'],
    link_args: ['-Wl,--gc-sections'],
  ),
)

test('default-window-size',
  executable('test-default-size',
    'tests/test_default_size.c', 'src/view.c', 'src/frame.c', 'src/pixbuf.c',
    protocol_headers,
    include_directories: include_directories('src'),
    dependencies: [decor_dep, wayland_server, wlroots, libdrm],
    c_args: ['-ffunction-sections', '-fdata-sections'],
    link_args: ['-Wl,--gc-sections'],
  ),
)

test('snapshot-output-state',
  executable('test-snapshot-state', 'tests/test_snapshot.c', 'src/pixbuf.c',
    protocol_headers,
    include_directories: include_directories('src'),
    dependencies: [decor_dep, wayland_server, wlroots, libdrm],
    link_args: ['-Wl,--wrap=wlr_output_from_resource',
      '-Wl,--wrap=wlr_output_layout_get_box'],
  ),
)

test('collar-hotkey',
  executable('test-hotkey',
    'tests/test_hotkey.c', 'src/hotkey.c',
    include_directories: include_directories('src'),
    dependencies: [wlroots, xkbcommon],
  ),
)

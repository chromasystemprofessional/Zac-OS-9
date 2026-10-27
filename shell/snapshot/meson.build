# Screen Snapshot (Apple menu): drag out a rectangle; a picture on the
# Desktop and the clipboard (slurp, native committed-frame capture, wl-copy).
configure_file(input: 'zacos9-snapshot', output: 'zacos9-snapshot', copy: true)
install_data('zacos9-snapshot', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
capture_exe = executable('zacos9-capture', 'capture.cpp',
  snapshot_protocol,
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  include_directories: include_directories('../finder'),
  install: true,
)
test('snapshot-image',
  executable('test-snapshot-image', '../../tests/finder/test_snapshot_image.cpp',
    snapshot_protocol,
    dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
    include_directories: include_directories('../finder')),
  env: ['QT_QPA_PLATFORM=offscreen'],
)
snapshot_animation = executable('test-snapshot-animation',
  '../../tests/finder/snapshot_animation.cpp',
  dependencies: [qt6])
test('native-snapshot', find_program('python3'),
  args: [files('../../tests/finder/test_snapshot.py'), compositor_exe, capture_exe,
    files('zacos9-snapshot'), snapshot_animation],
  timeout: 60,
)

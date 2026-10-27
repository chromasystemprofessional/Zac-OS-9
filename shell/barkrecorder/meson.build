executable('zacos9-barkrecorder',
  'main.cpp', 'barkrecorder.cpp',
  include_directories: include_directories('../finder'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)
test('bark-recorder',
  executable('test-bark-recorder', '../../tests/barkrecorder/test_barkrecorder.cpp',
    'barkrecorder.cpp',
    dependencies: [qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
)

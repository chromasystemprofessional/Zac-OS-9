executable('zacos9-stash',
  'main.cpp', 'stash.cpp', 'stashwindow.cpp',
  include_directories: include_directories('../finder'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)
test('stash',
  executable('test-stash', '../../tests/stash/test_stash.cpp', 'stash.cpp', 'stashwindow.cpp',
    dependencies: [qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
)

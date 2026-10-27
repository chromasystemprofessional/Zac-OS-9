executable('zacos9-notepad',
  'main.cpp', 'notepadwindow.cpp',
  include_directories: include_directories('../finder'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)
test('notepad',
  executable('test-notepad', 'test_notepad.cpp', 'notepadwindow.cpp',
    dependencies: [qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
)

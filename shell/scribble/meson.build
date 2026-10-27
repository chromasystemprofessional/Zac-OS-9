# Scribble (SimpleText) and Dog Ears (Stickies).
scribble_exe = executable('zacos9-scribble',
  'main.cpp', 'scribblewindow.cpp', 'textfile.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../print', '../panels'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)
test('scribble-speech',
  executable('test-scribble-speech',
    '../../tests/scribble/test_speech.cpp', 'scribblewindow.cpp', 'textfile.cpp',
    link_with: panelkit,
    include_directories: include_directories('../finder', '../print', '../panels'),
    dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 30,
)
test('scribble-files',
  executable('test-scribble-files', '../../tests/scribble/test_textfile.cpp', 'textfile.cpp',
    dependencies: [qt6],
  ),
)

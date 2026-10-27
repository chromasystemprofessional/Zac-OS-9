executable('zacos9-picture',
  'main.cpp',
  'picturewindow.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../print', '../panels'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)
picture_test = executable('test-picture', 'tests/test_picture.cpp',
  'picturewindow.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../print', '../panels'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
)
test('picture-viewer', picture_test, env: ['QT_QPA_PLATFORM=offscreen'], timeout: 30)

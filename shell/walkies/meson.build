# Walkies: places, each with its network, sound, time zone, printer and
# startup set (walkies/location.cpp).
executable('zacos9-walkies',
  'main.cpp', 'location.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../panels'),
  dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6, wayland_client, gio, gio_unix],
  install: true,
)
test('walkies-locations',
  executable('test-walkies-locations', 'test_location.cpp', 'location.cpp',
    dependencies: [shell_common_dep, platinum_dep, qt6],
  ),
  timeout: 10,
)

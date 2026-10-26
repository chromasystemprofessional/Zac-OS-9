# The Pack: Mac OS 9's Application palette, torn off the Application menu.
pack_exe = executable('zacos9-pack',
  'main.cpp', 'packwindow.cpp', 'apps.cpp', 'toplevels.cpp',
  client_protocol_src,
  include_directories: include_directories('../finder', '../print'),
  dependencies: [shell_common_dep, platinum_dep, qt6, layershellqt, wayland_client, gio, gio_unix],
  install: true,
)
test('pack-apps',
  executable('test-pack-apps', 'tests/test_apps.cpp', 'apps.cpp',
    dependencies: [platinum_dep, qt6, gio, gio_unix],
  ),
)

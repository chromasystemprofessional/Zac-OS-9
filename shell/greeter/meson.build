# The login window greetd shows (session/zacos9-greeter runs it).
executable('zacos9-login',
  'main.cpp',
  'greetd.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../print', '../panels'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
  install_dir: get_option('libexecdir') / 'zacos9',
)

# Talking to greetd, against a stand-in for its socket.
test('greetd-client',
  executable('test-greetd',
    'tests/test_greetd.cpp',
    'greetd.cpp',
    dependencies: [qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)

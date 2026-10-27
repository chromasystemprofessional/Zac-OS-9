configure_file(input: 'zacos9-patterns', output: 'zacos9-patterns', copy: true)
configure_file(input: 'zacos9-soundthemes', output: 'zacos9-soundthemes', copy: true)
install_data('zacos9-soundthemes', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
test('sound-themes', find_program('python3'),
  args: [files('../../tests/panels/test_soundthemes.py'), files('zacos9-soundthemes')],
  timeout: 60,
)
test('custom-themes',
  executable('test-customthemes', '../../tests/panels/test_customthemes.cpp', 'appearance.cpp',
    link_with: panelkit,
    include_directories: include_directories('../common'),
    dependencies: [platinum_dep, qt6],
  ),
  args: [meson.project_source_root() / 'assets/sounds/woodblock.wav'],
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)
install_data('zacos9-patterns', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
test('custom-patterns', find_program('python3'),
  args: [files('../../tests/panels/test_patterns.py'), files('zacos9-patterns')],
  timeout: 60,
)
test('custom-pattern-catalog',
  executable('test-custompatterns', '../../tests/panels/test_custompatterns.cpp', 'appearance.cpp',
    link_with: panelkit,
    include_directories: include_directories('../common'),
    dependencies: [platinum_dep, qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 30,
)

executable('zacos9-appearance',
  'main.cpp',
  'appearance.cpp',
  link_with: panelkit,
  include_directories: include_directories('../common'), # pixels.h
  dependencies: [platinum_dep, qt6],
  install: true,
)

executable('zacos9-collarpanel',
  'collarpanel_main.cpp',
  'collarpanel.cpp',
  '../collar/collarart.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../print', '../collar'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)

executable('zacos9-datetime',
  'datetime_main.cpp',
  'datetime.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)

# File Exchange: which application opens each file type (GIO, the
# user's mimeapps.list).
executable('zacos9-fileexchange',
  'fileexchange_main.cpp',
  'fileexchange.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder'),
  dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6, wayland_client, gio, gio_unix],
  install: true,
)

executable('zacos9-tcpip',
  'tcpip.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)

# Bluetooth: BlueZ over the system D-Bus; headsets and speakers become the
# sound output (pactl).
executable('zacos9-bluetooth',
  'bluetooth.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)

# Keychain Access: the Secret Service's keychains and their items
# (collar/keychain.cpp, shared with The Collar's Keychain module).
executable('zacos9-keychain',
  'keychainaccess.cpp',
  '../collar/keychain.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../collar'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)

executable('zacos9-filesharing',
  'filesharing.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)

# Screen Sharing: the panel, and the host it starts (wayvnc behind it).
executable('zacos9-screensharing',
  'screensharing.cpp',
  '../screenshare/common.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../screenshare'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)
test('internet-defaults',
  executable('test-internet-defaults', 'tests/test_defaultapps.cpp', 'defaultapps.cpp',
    dependencies: [qt6, gio, gio_unix],
  ),
)

# The Keyboard panel's layouts, read as zacos9-wm reads them.
xkb_layouts_sources = files('xkblayouts.cpp', '../../compositor/src/kblayout.c')
# The Internet panel's defaults, through GIO.
default_apps_sources = ['defaultapps.cpp', 'functionkeys.cpp']

startup_disk_sources = ['startupdisk.cpp']
memory_sources = ['memory.cpp']
# Memory with a fake helper (memory.cpp).
test('memory-panel',
  executable('test-memory-panel', '../../tests/memory/test_panel.cpp', memory_sources,
    link_with: panelkit,
    include_directories: include_directories('../common', '../print'),
    dependencies: [platinum_dep, qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 30,
)
# Startup Disk with a fake helper (startupdisk.cpp).
test('startupdisk-panel',
  executable('test-startupdisk-panel', '../../tests/startupdisk/test_panel.cpp', startup_disk_sources,
    link_with: panelkit,
    include_directories: include_directories('../finder', '../print'),
    dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 30,
)
crypt = dependency('libxcrypt')
users_sources = ['users.cpp']
# Users with a stand-in accountsservice on a private session bus (users.cpp).
test('users-panel', find_program('dbus-run-session'),
  args: ['--', executable('test-users-panel', '../../tests/users/test_panel.cpp', users_sources,
      link_with: panelkit,
      include_directories: include_directories('../finder'),
      dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client, crypt, gio_unix],
    ).full_path(), files('../../tests/users/fake_accounts.py')],
  timeout: 60,
)
controlpanel_exe = executable('zacos9-controlpanel',
  'controlpanel.cpp',
  # The panels, each registering itself (panelregistry.h).
  'mouse.cpp', 'trackpad.cpp', 'tablet.cpp', 'keyboard.cpp', 'sound.cpp',
  'monitors.cpp', 'energysaver.cpp', 'screensaver.cpp', 'zacmenu.cpp',
  'general.cpp', 'internet.cpp', 'accessibility.cpp', 'locale.cpp', 'speech.cpp',
  'soundclient.cpp',
  users_sources,
  xkb_layouts_sources, default_apps_sources, startup_disk_sources, memory_sources,
  link_with: panelkit,
  include_directories: [include_directories('../finder'), xkb_layouts_include],
  dependencies: [shell_common_dep, platinum_dep, qt6, xkbcommon_client, gio, gio_unix, wayland_client, crypt],
  install: true,
)

test('panel-hints',
  executable('test-panel-hints', 'tests/test_hints.cpp',
    link_with: panelkit,
    include_directories: include_directories('../common'),
    dependencies: [platinum_dep, qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'], timeout: 30,
)

test('collar-panel',
  executable('test-collar-panel',
    'tests/test_collarpanel.cpp',
    'collarpanel.cpp',
    '../collar/collarart.cpp',
    link_with: panelkit,
    include_directories: include_directories('../finder', '../print', '../collar'),
    dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'], timeout: 30,
)

test('keyboard-layout-list',
  executable('test-xkblayouts',
    'tests/test_xkblayouts.cpp', xkb_layouts_sources,
    include_directories: [xkb_layouts_include],
    dependencies: [platinum_dep, qt6, xkbcommon_client],
  ),
)

test('sound-output',
  executable('test-sound-output',
    'tests/test_sound.cpp',
    'soundclient.cpp',
    dependencies: qt6,
  ),
  args: files('tests/pactl-fixture.py'),
  timeout: 60,
)

collar_sources = [
  'collar.cpp', 'collarart.cpp', 'systemcontrols.cpp',
  'displaycontrols.cpp', 'kdeconnect.cpp', 'keychain.cpp', 'media.cpp',
  'filesharing.cpp', 'printing.cpp', 'extensions.cpp', '../panels/sharingclient.cpp', '../panels/soundclient.cpp',
]
collar_exe = executable('zacos9-collar', 'main.cpp', collar_sources,
  link_with: panelkit,
  include_directories: include_directories('../finder', '../panels', '../print'),
  dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6, layershellqt, wayland_client],
  install: true,
)

collar_ui_test = executable('test-collar', '../../tests/collar/test_collar.cpp', collar_sources,
  link_with: panelkit,
  include_directories: include_directories('../finder', '../panels', '../print'),
  dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6, layershellqt, wayland_client],
)
# No media players, File Sharing settings or printers from the computer running the tests.
collar_test_env = ['ZACOS9_MPRIS_PREFIX=org.zacos9.test.NoMpris.', 'ZACOS9_SHARING_CONF=/nonexistent',
  'ZACOS9_CUPS_BIN=' + meson.current_source_dir() / '../../tests/collar/no-printers']
test('collar-ui', collar_ui_test,
  env: ['QT_QPA_PLATFORM=offscreen', 'ZACOS9_KDECONNECT_SERVICE=org.zacos9.test.NoKdeConnect',
    'ZACOS9_SECRETS_SERVICE=org.zacos9.test.NoSecrets',
    'ZACOS9_COLLAR_EXTENSION_PREFIX=org.zacos9.test.NoExtensions'] + collar_test_env, timeout: 30)
collar_kdeconnect_test = executable('test-collar-kdeconnect', '../../tests/collar/test_kdeconnect.cpp',
  collar_sources,
  link_with: panelkit,
  include_directories: include_directories('../finder', '../panels', '../print'),
  dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6, layershellqt, wayland_client],
)
test('collar-kdeconnect', find_program('dbus-run-session'),
  args: ['--', collar_kdeconnect_test],
  env: ['QT_QPA_PLATFORM=offscreen', 'ZACOS9_SECRETS_SERVICE=org.zacos9.test.NoSecrets'] + collar_test_env,
  timeout: 60,
)
collar_keychain_test = executable('test-collar-keychain', '../../tests/collar/test_keychain.cpp',
  collar_sources,
  link_with: panelkit,
  include_directories: include_directories('../finder', '../panels', '../print'),
  dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6, layershellqt, wayland_client],
)
test('collar-keychain', find_program('dbus-run-session'),
  args: ['--', collar_keychain_test], env: ['QT_QPA_PLATFORM=offscreen'] + collar_test_env, timeout: 60,
)
collar_media_test = executable('test-collar-media', '../../tests/collar/test_media_sharing.cpp',
  collar_sources,
  link_with: panelkit,
  include_directories: include_directories('../finder', '../panels', '../print'),
  dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6, layershellqt, wayland_client],
)
test('collar-media-sharing', find_program('dbus-run-session'),
  args: ['--', collar_media_test], env: ['QT_QPA_PLATFORM=offscreen'], timeout: 60,
)
collar_extensions_test = executable('test-collar-extensions', '../../tests/collar/test_extensions.cpp',
  collar_sources,
  link_with: panelkit,
  include_directories: include_directories('../finder', '../panels', '../print'),
  dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6, layershellqt, wayland_client],
)
test('collar-extensions', find_program('dbus-run-session'),
  args: ['--', collar_extensions_test], env: ['QT_QPA_PLATFORM=offscreen'] + collar_test_env, timeout: 60,
)
collar_controls_test = executable('test-collar-controls',
  '../../tests/collar/test_controls.cpp', 'systemcontrols.cpp',
  dependencies: [qt6],
)
test('collar-controls', find_program('dbus-run-session'),
  args: ['--', collar_controls_test], timeout: 30,
)
collar_displays_test = executable('test-collar-displays',
  '../../tests/collar/test_displays.cpp', 'displaycontrols.cpp',
  dependencies: [platinum_dep, qt6],
)
test('collar-displays', find_program('dbus-run-session'),
  args: ['--', collar_displays_test, files('../../tests/collar/fake-ddcutil.sh')], timeout: 30,
)

test('collar-native', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(), files('../../tests/collar/test_native.py'),
    compositor_exe, collar_exe, capture_exe, vptr_exe],
  timeout: 90,
)

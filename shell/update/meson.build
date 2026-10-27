# Software Update (Apple menu): ZacOS 9 releases from GitHub, Debian's
# updates through apt; installs through zacos9-appstore-helper.

# Software Update's checks: reading a GitHub release and apt's simulation,
# and Debian version order (dpkg's own); no network, nothing installed.
test('update-check',
  executable('test-update-check',
    '../../tests/update/test_updatecheck.cpp',
    'updatecheck.cpp',
    dependencies: [shell_common_dep, qt6],
  ),
  timeout: 60,
)

# Software Update's daily check (a user timer; Check daily turns it off).
install_data('zacos9-update-check.service', 'zacos9-update-check.timer',
  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'user')

executable('zacos9-update',
  'main.cpp',
  'update.cpp',
  'updatecheck.cpp',
  '../store/storeclient.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../print', '../panels', '../store'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)

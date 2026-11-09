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

# What's New after a restart: shown once per version at login (a system
# autostart entry; turning it off in Startup Items turns the popup off).
# The notes for each version ship in the package; the seen version is
# remembered in ~/.config/zacos9/whatsnew-seen.
install_data('zacos9-whatsnew.desktop',
  install_dir: get_option('sysconfdir') / 'xdg' / 'autostart')
install_data('whatsnew/1.7.0.txt', 'whatsnew/1.7.1.txt', 'whatsnew/1.7.2.txt', 'whatsnew/1.7.3.txt', 'whatsnew/1.7.4.txt', 'whatsnew/1.7.5.txt', 'whatsnew/1.7.6.txt', 'whatsnew/1.8.0.txt', 'whatsnew/1.11.0.txt', 'whatsnew/1.12.0.txt', 'whatsnew/1.13.0.txt', 'whatsnew/1.14.0.txt',
  install_dir: get_option('datadir') / 'zacos9' / 'whatsnew')

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

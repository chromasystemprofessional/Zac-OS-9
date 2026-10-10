# The login session: a wayland-sessions entry that runs zacos9-session.
install_data('zacos9-session', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
install_data('zacos9.desktop',
  install_dir: get_option('datadir') / 'wayland-sessions')
install_data('zacos9-desktop-places', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
install_data('zacos9-screen-sharing', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
install_data('zacos9-rdp-helper', install_dir: get_option('libexecdir') / 'zacos9',
  install_mode: 'rwxr-xr-x')
install_data('zacos9-rdp-test', install_dir: get_option('libexecdir') / 'zacos9',
  install_mode: 'rwxr-xr-x')
install_data('zacos9-rdp-rfb', install_dir: get_option('libexecdir') / 'zacos9',
  install_mode: 'rwxr-xr-x')
install_data('org.zacos9.rdp.policy',
  install_dir: get_option('datadir') / 'polkit-1' / 'actions')
test('rdp-config', find_program('python3'),
  args: [files('../tests/finder/test_rdp_config.py'), files('zacos9-rdp-helper')])
test('desktop-places', find_program('python3'),
  args: [files('../tests/finder/test_desktop_places.py'), files('zacos9-desktop-places')])
test('desktop-gtk', find_program('python3'),
  args: [files('../tests/finder/test_desktop_gtk.py'), files('zacos9-desktop-places')],
  timeout: 30)
# The login screen's command (greetd's default_session).
install_data('zacos9-greeter', install_dir: get_option('libexecdir') / 'zacos9',
  install_mode: 'rwxr-xr-x')

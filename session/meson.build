# The login session: a wayland-sessions entry that runs zacos9-session.
install_data('zacos9-session', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
install_data('zacos9.desktop',
  install_dir: get_option('datadir') / 'wayland-sessions')
install_data('zacos9-desktop-places', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
install_data('zacos9-shutdown-items', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
install_data('zacos9-web-sharing', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
test('desktop-places', find_program('python3'),
  args: [files('../tests/finder/test_desktop_places.py'), files('zacos9-desktop-places')])
test('desktop-gtk', find_program('python3'),
  args: [files('../tests/finder/test_desktop_gtk.py'), compositor_exe.full_path(),
    files('zacos9-desktop-places')],
  depends: compositor_exe,
  timeout: 30)
test('shutdown-items', find_program('python3'),
  args: [files('../tests/session/test_shutdown_items.py'), files('zacos9-shutdown-items')],
  timeout: 15)
test('safe-mode-session', find_program('python3'),
  args: [files('../tests/session/test_safe_mode.py'), files('zacos9-session')],
  timeout: 15)
test('web-sharing', find_program('python3'),
  args: [files('../tests/session/test_web_sharing.py'), files('zacos9-web-sharing')],
  timeout: 30)
# The login screen's command (greetd's default_session).
install_data('zacos9-greeter', install_dir: get_option('libexecdir') / 'zacos9',
  install_mode: 'rwxr-xr-x')

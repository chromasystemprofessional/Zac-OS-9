# The login session: a wayland-sessions entry that runs zacos9-session.
install_data('zacos9-session', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
install_data('zacos9.desktop',
  install_dir: get_option('datadir') / 'wayland-sessions')
# The login screen's command (greetd's default_session).
install_data('zacos9-greeter', install_dir: get_option('libexecdir') / 'zacos9',
  install_mode: 'rwxr-xr-x')

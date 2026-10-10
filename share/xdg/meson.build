# Default settings for other programs in a ZacOS 9 session, found through
# XDG_CONFIG_DIRS (set by zacos9-session).
test('screen-sharing-config', find_program('python3'),
  args: [files('../../tests/finder/test_screen_sharing_config.py'), meson.project_source_root()])
test('screen-sharing-session', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(),
    files('../../tests/finder/test_screencast.py'), compositor_exe.full_path()],
  depends: [compositor_exe],
  timeout: 60)
# Folders open in the Finder (xdg-open, GIO): ZacOS 9's own default
# applications, read as $XDG_CURRENT_DESKTOP-mimeapps.list.
install_data('zacos9-mimeapps.list', install_dir: get_option('datadir') / 'zacos9' / 'xdg')
install_data('foot/foot.ini',
  install_dir: get_option('datadir') / 'zacos9' / 'xdg' / 'foot')
install_data('xdg-desktop-portal/zacos9-portals.conf',
  install_dir: get_option('datadir') / 'zacos9' / 'xdg' / 'xdg-desktop-portal')
install_data('xdg-desktop-portal/portals/zacos9.portal',
  install_dir: get_option('datadir') / 'xdg-desktop-portal' / 'portals')
portal_service = configuration_data()
portal_service.set('BINDIR', get_option('prefix') / get_option('bindir'))
configure_file(
  input: 'dbus-1/services/org.freedesktop.impl.portal.desktop.zacos9.service',
  output: 'org.freedesktop.impl.portal.desktop.zacos9.service',
  configuration: portal_service,
  install_dir: get_option('datadir') / 'dbus-1' / 'services',
)

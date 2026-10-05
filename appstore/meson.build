# The Software window (shell/store/zacos9-store): the privileged
# helper behind it, and the polkit action and rule that let administrators
# run it without a password (this desktop runs no polkit agent to ask
# for one), the same way sharing/ does for the File Sharing panel.
install_data('zacos9-appstore-helper',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
# Look-and-feel presets (the Software window's second button): runs as the
# user, no privileges.
install_data('zacos9-appstyle',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
test('appstyle', find_program('python3'),
  args: [files('../tests/store/test_appstyle.py'), files('zacos9-appstyle')])
install_data('zacos9-software-catalog',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
test('software-catalog', find_program('python3'),
  args: [files('../tests/store/test_software_catalog.py'), files('zacos9-software-catalog')])
install_data('org.zacos9.appstore.policy',
  install_dir: get_option('datadir') / 'polkit-1' / 'actions')
install_data('50-zacos9-appstore.rules',
  install_dir: get_option('datadir') / 'polkit-1' / 'rules.d')

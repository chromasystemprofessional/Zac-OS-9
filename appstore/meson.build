# The Software window (shell/store/zacos9-store): the privileged
# helper behind it, and the polkit action and rule that let administrators
# run it without a password (this desktop runs no polkit agent to ask
# for one), the same way sharing/ does for the File Sharing panel.
install_data('zacos9-appstore-helper',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
install_data('org.zacos9.appstore.policy',
  install_dir: get_option('datadir') / 'polkit-1' / 'actions')
install_data('50-zacos9-appstore.rules',
  install_dir: get_option('datadir') / 'polkit-1' / 'rules.d')

# File sharing: the privileged helper behind the File Sharing control panel,
# and the polkit action and rule that let administrators run it.
install_data('zacos9-sharing-helper',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
install_data('org.zacos9.sharing.policy',
  install_dir: get_option('datadir') / 'polkit-1' / 'actions')
install_data('50-zacos9-sharing.rules',
  install_dir: get_option('datadir') / 'polkit-1' / 'rules.d')

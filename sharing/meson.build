# File sharing: the privileged helper behind the File Sharing control panel,
# and the polkit action and rule that let administrators run it.
install_data('platinum-sharing-helper',
  install_dir: get_option('libexecdir') / 'platinum', install_mode: 'rwxr-xr-x')
install_data('org.platinum2026.sharing.policy',
  install_dir: get_option('datadir') / 'polkit-1' / 'actions')
install_data('50-platinum-sharing.rules',
  install_dir: get_option('datadir') / 'polkit-1' / 'rules.d')

# Accounts: the privileged helper behind the Users control panel's
# computer-wide settings, and the polkit action and rule that let
# administrators run it.
install_data('zacos9-accounts-helper',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
install_data('org.zacos9.accounts.policy',
  install_dir: get_option('datadir') / 'polkit-1' / 'actions')
install_data('50-zacos9-accounts.rules',
  install_dir: get_option('datadir') / 'polkit-1' / 'rules.d')

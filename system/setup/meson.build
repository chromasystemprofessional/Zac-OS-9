# The Setup Assistant's root helper (shell/setup is its window).
install_data('zacos9-setup-helper',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
install_data('org.zacos9.setup.policy',
  install_dir: get_option('datadir') / 'polkit-1' / 'actions')
install_data('50-zacos9-setup.rules',
  install_dir: get_option('datadir') / 'polkit-1' / 'rules.d')

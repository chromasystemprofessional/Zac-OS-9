# The Extensions Manager's privileged helper for turning kernel modules
# on or off at startup, and the polkit action/rule for it.
install_data('zacos9-modules-helper',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
install_data('org.zacos9.modules.policy',
  install_dir: get_option('datadir') / 'polkit-1' / 'actions')
install_data('50-zacos9.modules.rules',
  install_dir: get_option('datadir') / 'polkit-1' / 'rules.d')

test('modules-helper', find_program('sh'),
  args: [files('../../tests/system/test_modules_helper.sh'), meson.current_source_dir() / 'zacos9-modules-helper'])

# Memory: the privileged helper behind the Memory control panel's
# Compressed Memory switch, and the polkit action and rule that let
# administrators run it.
install_data('zacos9-memory-helper',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
install_data('org.zacos9.memory.policy',
  install_dir: get_option('datadir') / 'polkit-1' / 'actions')
install_data('50-zacos9-memory.rules',
  install_dir: get_option('datadir') / 'polkit-1' / 'rules.d')

test('memory-helper', find_program('sh'),
  args: [files('test_helper.sh'), meson.current_source_dir() / 'zacos9-memory-helper'])

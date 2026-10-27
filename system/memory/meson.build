# Memory: the control panel's privileged helper (virtual memory, the RAM
# Disk, earlyoom), the polkit action and rule that let administrators run
# it, and the unit that makes the RAM Disk at startup and saves it at shut
# down (debhelper enables it).
install_data('zacos9-memory-helper',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
install_data('org.zacos9.memory.policy',
  install_dir: get_option('datadir') / 'polkit-1' / 'actions')
install_data('50-zacos9-memory.rules',
  install_dir: get_option('datadir') / 'polkit-1' / 'rules.d')
install_data('zacos9-ramdisk.service',
  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')

test('memory-helper', find_program('python3'),
  args: [files('../../tests/memory/test_helper.py'), files('zacos9-memory-helper')])

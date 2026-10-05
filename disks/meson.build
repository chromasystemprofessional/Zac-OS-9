install_data('zacos9-disk-helper',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
disk_policy = configuration_data()
disk_policy.set('DISK_HELPER', get_option('prefix') / get_option('libexecdir') / 'zacos9' / 'zacos9-disk-helper')
configure_file(input: 'org.zacos9.disks.policy.in',
  output: 'org.zacos9.disks.policy', configuration: disk_policy,
  install_dir: get_option('datadir') / 'polkit-1' / 'actions')
test('disk-helper', find_program('python3'),
  args: [files('test_disk_helper.py'), files('zacos9-disk-helper')])

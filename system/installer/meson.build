install_data('zacos9-install', 'zacos9-release-disk',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
install_data('org.zacos9.installer.policy',
  install_dir: get_option('datadir') / 'polkit-1' / 'actions')
install_data('50-zacos9-installer.rules',
  install_dir: get_option('datadir') / 'polkit-1' / 'rules.d')

# Freeing a disk the live desktop has mounted, for the installer (issue #31).
test('release-disk', find_program('sh'),
  args: [files('../../tests/installer/test_release_disk.sh'), files('zacos9-release-disk')])

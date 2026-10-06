# Mounting internal drives without a password prompt (the Finder mounts
# every disk at startup; see shell/finder/localvolumes.h).
install_data('50-zacos9-mount.rules',
  install_dir: get_option('datadir') / 'polkit-1' / 'rules.d')
test('mount-authorization', find_program('python3'),
  args: [files('test_mount_rule.py'), files('50-zacos9-mount.rules')])

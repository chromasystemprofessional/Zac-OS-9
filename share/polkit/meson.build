# Mounting internal drives without a password prompt (the Finder mounts
# every disk at startup; see shell/finder/localvolumes.h).
install_data('50-zacos9-mount.rules',
  install_dir: get_option('datadir') / 'polkit-1' / 'rules.d')
test('mount-authorization', find_program('python3'),
  args: [files('test_mount_rule.py'), files('50-zacos9-mount.rules')])
# Stopping and starting printers (shell/print/printers.cpp, cups-pk-helper).
install_data('50-zacos9-printing.rules',
  install_dir: get_option('datadir') / 'polkit-1' / 'rules.d')
test('printing-authorization', find_program('python3'),
  args: [files('test_mount_rule.py'), files('50-zacos9-printing.rules'),
    'org.opensuse.cupspkhelper.mechanism.printer-enable',
    'org.opensuse.cupspkhelper.mechanism.printer-set-default'])

# Startup Disk: the privileged helper that reads and sets GRUB's default
# entry, and the polkit action and rule that let administrators run it.
install_data('zacos9-startupdisk-helper',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
install_data('org.zacos9.startupdisk.policy',
  install_dir: get_option('datadir') / 'polkit-1' / 'actions')
install_data('50-zacos9-startupdisk.rules',
  install_dir: get_option('datadir') / 'polkit-1' / 'rules.d')

test('startupdisk-helper', find_program('python3'),
  args: [files('../tests/startupdisk/test_helper.py'), files('zacos9-startupdisk-helper'),
    files('../tests/startupdisk/grub.cfg')])
# Old Dog's launcher and the startup disk the panel chose for it.
test('startupdisk-classic', find_program('python3'),
  args: [files('../tests/startupdisk/test_classic.py'), files('../shell/classic/zacos9-classic')])

# The boot screens. The boot splash: a plymouth theme with the logo and a
# progress bar on white; zacos9-wm takes over with the logo in the same
# place (compositor/src/startup.c). logo.png is generated from the ZacOS 9
# SVG. Before it, GRUB shows the same logo too: on the live medium from
# iso/config/bootloaders; on an installed system from a picture made for the
# screen by zacos9-bootlogo (run by zacos9-install) and shown by 09_zacos9.
install_data('plymouth/zacos9.plymouth', 'plymouth/zacos9.script',
  'plymouth/logo.png', 'plymouth/progress-box.png', 'plymouth/progress-fill.png',
  install_dir: get_option('datadir') / 'plymouth' / 'themes' / 'zacos9')

install_data('zacos9-bootlogo',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
install_data('logo-64.rgb',
  install_dir: get_option('datadir') / 'zacos9' / 'boot')
install_data('09_zacos9',
  install_dir: get_option('sysconfdir') / 'grub.d', install_mode: 'rwxr-xr-x')

test('boot-splash-displays', find_program('python3'),
  args: [files('tests/test_splash.py'), files('plymouth/zacos9.script')])

# The boot screens. The boot splash: a plymouth theme with the logo and a
# progress bar on white; zacos9-wm takes over with the logo in the same
# place (compositor/src/startup.c). logo.png is generated from the ZacOS 9
# SVG. Before it, GRUB shows the same logo too: on the live medium from
# iso/config/bootloaders; on an installed system from a picture made for the
# screen by zacos9-bootlogo (run by zacos9-install) and shown by 09_zacos9.
install_data('plymouth/zacos9.plymouth', 'plymouth/zacos9.script',
  'plymouth/logo.png', 'plymouth/progress-box.png', 'plymouth/progress-fill.png',
  install_dir: get_option('datadir') / 'plymouth' / 'themes' / 'zacos9')

# The splash's Welcome screen, drawn now by the compositor's own code
# (boot/make-splash.c), so the two match when one hands over to the other.
make_splash = executable('make-splash', 'make-splash.c', dependencies: [platinum_dep])
custom_target('splash-art',
  output: ['welcome.png', 'bar-fill.png', 'bar-end.png', 'pattern.png', 'parade.png'],
  command: [make_splash, '@OUTDIR@'],
  install: true,
  install_dir: get_option('datadir') / 'plymouth' / 'themes' / 'zacos9')

# The live extension parade at boot (boot/zacos9-parade.c); debhelper
# enables the unit.
parade = executable('zacos9-parade', 'zacos9-parade.c', dependencies: [platinum_dep],
  install: true, install_dir: get_option('libexecdir') / 'zacos9')
install_data('zacos9-parade.service',
  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
test('boot-parade', find_program('python3'),
  args: [files('tests/test_parade.py'), parade, files('../lib/extensions.c')])

# Handing the startup screen from the splash to the compositor.
install_data('zacos9-boot-handoff',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
install_data('zacos9-boot-handoff.service',
  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('plymouth-quit.service.d/zacos9.conf',
  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system' / 'plymouth-quit.service.d')
install_data('plymouth-quit-wait.service.d/zacos9.conf',
  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system' / 'plymouth-quit-wait.service.d')
test('boot-handoff', find_program('python3'),
  args: [files('tests/test_handoff.py'), files('zacos9-boot-handoff')])

install_data('zacos9-bootlogo',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
install_data('logo-64.rgb',
  install_dir: get_option('datadir') / 'zacos9' / 'boot')
install_data('09_zacos9',
  install_dir: get_option('sysconfdir') / 'grub.d', install_mode: 'rwxr-xr-x')

test('boot-splash-displays', find_program('python3'),
  args: [files('tests/test_splash.py'), files('plymouth/zacos9.script')])
test('boot-splash-welcome', find_program('python3'),
  args: [files('tests/test_splash.py'), files('plymouth/zacos9.script'), 'boot'])

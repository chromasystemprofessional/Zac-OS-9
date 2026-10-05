# Hardware quirks: fixes for particular computers that the system, not the
# desktop, has to apply. See docs/hardware.md.
install_data('zacos9-hda-load', 'zacos9-gpe-guard',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
# Haswell HDMI audio: the driver loads through zacos9-hda-load.
install_data('zacos9-hda.conf',
  install_dir: get_option('prefix') / 'lib' / 'modprobe.d')
# ACPI interrupt storms on Macs; debhelper enables the timer.
install_data('zacos9-gpe-guard.service', 'zacos9-gpe-guard.timer',
  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
test('hardware-quirks', find_program('python3'),
  args: [files('../tests/hardware/test_quirks.py'),
         files('zacos9-hda-load'), files('zacos9-gpe-guard')])

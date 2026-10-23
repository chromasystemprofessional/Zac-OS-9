# Hardware quirks: fixes for particular computers that the system, not the
# desktop, has to apply. See docs/hardware.md.
install_data('zacos9-hda-load', 'zacos9-gpe-guard', 'zacos9-hdmi-watchdog',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
# Haswell HDMI audio: the driver loads through zacos9-hda-load.
install_data('zacos9-hda.conf',
  install_dir: get_option('prefix') / 'lib' / 'modprobe.d')
# Haswell HDMI audio that freezes mid-stream; debhelper enables it per user.
install_data('zacos9-hdmi-watchdog.service',
  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'user')
# ACPI interrupt storms on Macs; debhelper enables the timer.
install_data('zacos9-gpe-guard.service', 'zacos9-gpe-guard.timer',
  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
test('hardware-quirks', find_program('python3'),
  args: [files('../tests/hardware/test_quirks.py'),
         files('zacos9-hda-load'), files('zacos9-gpe-guard'),
         files('zacos9-hdmi-watchdog')])

# The built-in screen on the Intel GPU of a dual-GPU MacBook Pro
# (zacos9-intel-graphics, setos.efi); setos.efi needs gnu-efi to build.
install_data('intel-graphics/zacos9-intel-graphics',
  install_dir: get_option('libexecdir') / 'zacos9', install_mode: 'rwxr-xr-x')
if host_machine.cpu_family() == 'x86_64' and import('fs').exists('/usr/lib/crt0-efi-x86_64.o')
  custom_target('setos.efi',
    input: 'intel-graphics/setos.c',
    output: 'setos.efi',
    command: [find_program('intel-graphics/build-setos.sh'), '@OUTPUT@'],
    install: true,
    install_dir: get_option('prefix') / 'lib' / 'zacos9')
endif
test('intel-graphics', find_program('python3'),
  args: [files('../tests/hardware/test_intel_graphics.py'), files('intel-graphics/zacos9-intel-graphics')])

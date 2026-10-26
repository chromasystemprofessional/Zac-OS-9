# The classic Mac OS launcher, next to the Finder that runs it.
configure_file(input: 'zacos9-classic', output: 'zacos9-classic', copy: true)
install_data('zacos9-classic', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
test('classic-window-probe', find_program('python3'),
  args: [files('../../tests/emulation/test_classic_probe.py')],
  env: ['PYTHONDONTWRITEBYTECODE=1'],
)

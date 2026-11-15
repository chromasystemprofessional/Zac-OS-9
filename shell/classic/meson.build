# The classic Mac OS launcher, next to the Finder that runs it.
configure_file(input: 'zacos9-classic', output: 'zacos9-classic', copy: true)
install_data('zacos9-classic', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
executable('zacos9-classic-windows',
  'windows.cpp', '../pack/toplevels.cpp', client_protocol_src,
  include_directories: include_directories('../pack'),
  dependencies: [qt6, wayland_client],
  install: true,
)
test('classic-window-probe', find_program('python3'),
  args: [files('../../tests/emulation/test_classic_probe.py')],
  env: ['PYTHONDONTWRITEBYTECODE=1'],
)
test('classic-open', find_program('python3'),
  args: [files('../../tests/classic/test_open.py'), files('zacos9-classic')],
  env: ['PYTHONDONTWRITEBYTECODE=1'],
)

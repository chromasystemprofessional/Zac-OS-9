project('platinum-2026', 'c',
  version: '0.0.1',
  license: 'GPL-3.0-or-later',
  meson_version: '>=1.0.0',
  default_options: ['c_std=c11', 'warning_level=2', 'werror=false'],
)

add_project_arguments(
  '-DWLR_USE_UNSTABLE',
  '-D_POSIX_C_SOURCE=200809L',
  '-Wno-unused-parameter',
  language: 'c',
)

subdir('lib')
subdir('compositor')
subdir('shell')
subdir('share')
subdir('session')
subdir('boot')
subdir('sharing')

project('zacos9', 'c',
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

# Tests must never draw on the desktop they are built from: a test that opened
# windows on the live compositor once crashed it and logged the user out. Tests
# that need a display start their own headless zacos9-wm and override these.
add_test_setup('isolated',
  env: {'WAYLAND_DISPLAY': 'zacos9-test-no-display', 'DISPLAY': ''},
  is_default: true,
)

subdir('lib')
subdir('compositor')
subdir('shell')
subdir('share')
subdir('session')
subdir('boot')
subdir('hardware')
subdir('sharing')
subdir('appstore')
subdir('accounts')
subdir('startupdisk')
subdir('modules')
subdir('memory')
subdir('disks')
subdir('installer')
subdir('setup')
subdir('network')

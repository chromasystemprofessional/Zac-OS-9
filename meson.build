project('zacos9', 'c',
  version: '0.0.1',
  license: 'GPL-3.0-or-later',
  meson_version: '>=1.0.0',
  default_options: ['c_std=c11', 'cpp_std=c++17', 'warning_level=2', 'werror=false'],
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
  env: {'WAYLAND_DISPLAY': 'zacos9-test-no-display', 'DISPLAY': '',
    # The Python tests' shared helpers (tests/harness/zacos9test.py).
    'PYTHONPATH': meson.project_source_root() / 'tests/harness'},
  is_default: true,
)

subdir('lib')
subdir('compositor')
subdir('shell')
subdir('share')
subdir('session')
subdir('boot')
subdir('hardware')
subdir('system')
subdir('network')

# Everything the package installs, against tests/baseline/installed-files.txt
# (the restructuring's safety net: a file that drops out of the package fails).
test('installed-files', find_program('python3'),
  args: [files('tests/baseline/test_installed_files.py'), meson.project_build_root(),
    files('tests/baseline/installed-files.txt')])

# The Mac OS 9 theme extractor (theming step 3).
test('theme-extractor', find_program('python3'),
  args: [files('tests/tools/test_extract_macos9.py'),
    meson.project_source_root() / 'tools' / 'extract-macos9-theme.py'])

# The extractor the Theme Installer runs, and its decoder modules.
install_data(
  'tools/extract-macos9-theme.py',
  'tools/macicons.py',
  'tools/macpict.py',
  'tools/macsnd.py',
  'tools/macpat.py',
  'tools/pngwrite.py',
  install_dir: get_option('datadir') / 'zacos9' / 'tools')

# Mac OS 9 icon decoder (theming, phase A).
test('macicons', find_program('python3'),
  args: [files('tests/tools/test_macicons.py'),
    meson.project_source_root() / 'tools' / 'macicons.py'])

# Mac OS 9 PICT decoder (theming, phase A).
test('macpict', find_program('python3'),
  args: [files('tests/tools/test_macpict.py'),
    meson.project_source_root() / 'tools' / 'macpict.py'])

# Mac OS 9 sound decoder (theming, phase A).
test('macsnd', find_program('python3'),
  args: [files('tests/tools/test_macsnd.py'),
    meson.project_source_root() / 'tools' / 'macsnd.py'])

# Mac OS 9 pattern decoder (theming, phase A).
test('macpat', find_program('python3'),
  args: [files('tests/tools/test_macpat.py'),
    meson.project_source_root() / 'tools' / 'macpat.py'])

# PNG writer and scaling used by the theme extractor.
test('pngwrite', find_program('python3'),
  args: [files('tests/tools/test_pngwrite.py'),
    meson.project_source_root() / 'tools' / 'pngwrite.py'])

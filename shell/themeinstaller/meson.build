# The Theme Installer: builds a ZacOS theme from a Mac OS 9 install.
themeinstaller_exe = executable('zacos9-themeinstaller',
  'main.cpp',
  include_directories: include_directories('../common'),
  dependencies: [platinum_dep, qt6],
  install: true,
)

test('theme-installer', find_program('python3'),
  args: [files('../../tests/themeinstaller/test_themeinstaller.py'), themeinstaller_exe])

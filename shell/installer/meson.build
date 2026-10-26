installer_exe = executable('zacos9-installer',
  'main.cpp',
  'installer.cpp',
  # Disk Setup's plan and partition map, for "next to what's on it".
  '../disksetup/plan.cpp', '../disksetup/udisks.cpp', '../disksetup/diskmap.cpp',
  link_with: panelkit,
  include_directories: include_directories('../print', '../panels', '../disksetup'),
  dependencies: [shell_common_dep, platinum_dep, qt6],
  install: true,
)
test('installer-beside', find_program('python3'),
  args: [files('../../tests/installer/test_beside.py'), compositor_exe.full_path(), installer_exe, vptr_exe],
  depends: compositor_exe,
  timeout: 90)

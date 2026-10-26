# Disk Setup: First Aid (verify, repair, drive health) and partitioning,
# all through UDisks2 (disksetup/); the installer opens it too.
disksetup_exe = executable('zacos9-disksetup',
  'main.cpp', 'diskmap.cpp', 'udisks.cpp', 'plan.cpp',
  'inuse.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../panels'),
  dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)

test('disksetup-plan',
  executable('test-disksetup-plan', 'test_plan.cpp', 'plan.cpp', 'udisks.cpp',
    dependencies: [qt6],
  ),
  env: ['LC_ALL=en_US.UTF-8'], timeout: 10,
)
# The partition map against a stand-in UDisks: drag, +, Apply (vptr is defined further down).
disksetup_map_test = files('../../tests/disksetup/test_partition_map.py')
test('disksetup-udisks',
  executable('test-disksetup-udisks', 'test_udisks.cpp', 'udisks.cpp',
    'inuse.cpp',
    include_directories: include_directories('../finder'),
    dependencies: [shell_files_dep, qt6],
  ),
  env: ['LC_ALL=en_US.UTF-8'], timeout: 10,
)
test('disksetup-partition-map', find_program('python3'),
  args: [disksetup_map_test, compositor_exe.full_path(), disksetup_exe, vptr_exe],
  depends: compositor_exe,
  timeout: 90)

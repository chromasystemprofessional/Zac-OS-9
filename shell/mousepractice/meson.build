# Mouse Practice: Learn ZacOS's first lesson (mousepractice/practice.cpp
# plays it; docs/tutorial.md).
executable('zacos9-mousepractice',
  'main.cpp', 'practice.cpp',
  include_directories: include_directories('../finder'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)
test('mouse-practice',
  executable('test-mouse-practice', '../../tests/mousepractice/test_practice.cpp', 'practice.cpp',
    dependencies: [qt6],
  ),
  timeout: 10,
)

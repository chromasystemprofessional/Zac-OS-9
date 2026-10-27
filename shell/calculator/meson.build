# The Calculator desk accessory.
executable('zacos9-calculator',
  'main.cpp', 'engine.cpp',
  include_directories: include_directories('../common', '../print'),
  dependencies: [platinum_dep, qt6],
  install: true,
)
test('calculator',
  executable('test-calculator', '../../tests/calculator/test_engine.cpp', 'engine.cpp',
    dependencies: [qt6],
  ),
)

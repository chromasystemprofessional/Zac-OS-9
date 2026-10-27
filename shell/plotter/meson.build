# Plotter: graphs of y = f(x) (plotter/expr.cpp reads them).
executable('zacos9-plotter',
  'main.cpp', 'expr.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../panels'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)
test('plotter-expr',
  executable('test-plotter-expr', '../../tests/plotter/test_expr.cpp', 'expr.cpp',
    dependencies: [qt6]),
  timeout: 10)

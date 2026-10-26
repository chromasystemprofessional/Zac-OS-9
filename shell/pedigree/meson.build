# Pedigree (Apple System Profiler).
pedigree_sources = ['pedigreewindow.cpp', 'profile.cpp']
executable('zacos9-pedigree', 'main.cpp', pedigree_sources,
  link_with: panelkit,
  include_directories: include_directories('../finder', '../print', '../panels'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)
test('pedigree',
  executable('test-pedigree', 'tests/test_pedigree.cpp', pedigree_sources,
    link_with: panelkit,
    include_directories: include_directories('../finder', '../print', '../panels'),
    dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 30,
)

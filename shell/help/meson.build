# The Help Center (Mac Help).
help_sources = ['helpwindow.cpp', 'helpbook.cpp']
executable('zacos9-help', 'main.cpp', help_sources,
  link_with: panelkit,
  include_directories: include_directories('../finder', '../print', '../panels'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)
test('help-center',
  executable('test-help-center', 'tests/test_help.cpp', help_sources,
    link_with: panelkit,
    include_directories: include_directories('../finder', '../print', '../panels'),
    dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  ),
  env: ['QT_QPA_PLATFORM=offscreen', 'ZACOS9_HELP_SOURCE=' + meson.project_source_root() / 'share/zacos9/help'],
  timeout: 30,
)

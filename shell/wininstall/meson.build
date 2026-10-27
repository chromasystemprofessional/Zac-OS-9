executable('zacos9-wininstall',
  'main.cpp',
  'wininstall.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../print', '../panels'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)

executable('zacos9-appinstall',
  'main.cpp',
  'appinstall.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../print', '../panels'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)

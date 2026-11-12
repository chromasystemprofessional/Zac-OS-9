storeclient_lib = static_library('storeclient', 'storeclient.cpp',
  include_directories: include_directories('../finder', '../print', '../panels'),
  dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6, wayland_client, gio, gio_unix],
)

executable('zacos9-store',
  'main.cpp',
  'store.cpp',
  link_with: [panelkit, storeclient_lib],
  include_directories: include_directories('../finder', '../print', '../panels'),
  dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6, wayland_client, gio, gio_unix],
  install: true,
)

test('store-browser',
  executable('test-store-browser',
    '../../tests/store/test_browser.cpp',
    'store.cpp',
    link_with: [panelkit, storeclient_lib],
    include_directories: include_directories('../finder', '../print', '../panels'),
    dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6, wayland_client, gio, gio_unix],
    build_by_default: false,
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)

# The Software window's catalog: reading it, filtering, and the helper
# script's own package-name validation, run directly (never through
# pkexec, and never with a package name that could really install or
# remove anything).
test('store-client',
  executable('test-store-client',
    '../../tests/store/test_storeclient.cpp',
    link_with: storeclient_lib,
    include_directories: include_directories('../finder', '../print'), # settings.h
    dependencies: [shell_common_dep, platinum_dep, qt6],
    build_by_default: false,
  ),
  timeout: 60,
)

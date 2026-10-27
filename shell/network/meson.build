# Parsing avahi-browse and WS-Discovery replies, against recorded text
# (see the test file for what could and couldn't be checked live).
test('discovery',
  executable('test-discovery',
    '../../tests/network/test_discovery.cpp',
    'discovery.cpp',
    include_directories: include_directories('../common'),
    dependencies: [shell_common_dep, platinum_dep, qt6],
  ),
  timeout: 60,
)

# AFP volume names and mount points (files/netvolumes.cpp).
test('netvolumes',
  executable('test-netvolumes',
    '../../tests/network/test_netvolumes.cpp',
    dependencies: [shell_files_dep, qt6],
  ),
)

executable('zacos9-netbrowser',
  'main.cpp',
  'netbrowser.cpp',
  'connectdialog.cpp',
  'logindialog.cpp',
  'volumedialog.cpp',
  'discovery.cpp',
  'afpclient.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../print', '../panels'),
  dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)

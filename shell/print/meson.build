# Printing over CUPS: Printer Picker (the Chooser) and print queues.
print_sources = []
executable('zacos9-printerpicker', 'picker_main.cpp', 'pickerwindow.cpp', print_sources,
  link_with: panelkit,
  include_directories: include_directories('../finder', '../panels'),
  dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)
executable('zacos9-printqueue', 'queue_main.cpp', 'queuewindow.cpp', print_sources,
  link_with: panelkit,
  include_directories: include_directories('../finder', '../panels'),
  dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)
test('printing',
  executable('test-printing', '../../tests/print/test_printing.cpp', 'pickerwindow.cpp',
    'queuewindow.cpp', print_sources,
    link_with: panelkit,
    include_directories: include_directories('../finder', '../panels'),
    dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6, wayland_client],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)

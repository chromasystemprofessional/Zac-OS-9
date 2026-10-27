# zacos9-file-portal: the Open and Save dialogs other programs get through
# xdg-desktop-portal (its FileChooser interface).
# The chooser shows the startup disk through the Finder's Macintosh view.
file_chooser_sources = [
  'filechooserportal.cpp',
]
file_chooser_deps = [platinum_dep, qt6, gio, gio_unix, wayland_client, finder_fonts_dep]

file_portal = executable('zacos9-file-portal',
  'main.cpp',
  file_chooser_sources,
  link_with: panelkit,
  include_directories: include_directories('../print', '../network', '../panels'),
  dependencies: [shell_files_dep, shell_common_dep, file_chooser_deps],
  install: true,
)

test('filechooser-locations',
  executable('test-filechooser-portal',
    '../../tests/fileportal/test_filechooser_portal.cpp',
    file_chooser_sources,
    link_with: panelkit,
    include_directories: include_directories('../print', '../network', '../panels'),
    dependencies: [shell_files_dep, shell_common_dep, file_chooser_deps],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
)

test('filechooser-requests', find_program('dbus-run-session'),
  args: [executable('test-filechooser-requests',
    '../../tests/fileportal/test_filechooser_requests.cpp', file_chooser_sources,
    link_with: panelkit,
    include_directories: include_directories('../print', '../network', '../panels'),
    dependencies: [shell_files_dep, shell_common_dep, file_chooser_deps],
  )],
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 45,
)

test('filechooser-lifecycle', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(),
    files('../../tests/finder/test_filechooser_lifecycle.py'), file_portal.full_path()],
  depends: [file_portal],
  timeout: 30,
)

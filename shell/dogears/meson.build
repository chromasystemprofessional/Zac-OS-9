executable('zacos9-dogears',
  'main.cpp', 'notes.cpp', '../scribble/textfile.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../print', '../panels', '../scribble'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)
test('dog-ears-notes',
  executable('test-dog-ears-notes', 'tests/test_notes.cpp', 'notes.cpp',
    dependencies: [qt6],
  ),
)

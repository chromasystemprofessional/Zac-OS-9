# Key Tags (Key Caps), drawn from the same keymap as zacos9-wm's.
executable('zacos9-keytags',
  'main.cpp', 'keytags.cpp', 'keytagswindow.cpp', xkb_layouts_sources,
  include_directories: [include_directories('../finder', '../panels'), xkb_layouts_include],
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client, xkbcommon_client],
  install: true,
)
test('key-tags',
  executable('test-key-tags', 'test_keytags.cpp', 'keytags.cpp',
    'keytagswindow.cpp', xkb_layouts_sources,
    include_directories: [include_directories('../panels'), xkb_layouts_include],
    dependencies: [platinum_dep, qt6, xkbcommon_client],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
)

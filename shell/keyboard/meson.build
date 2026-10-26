# The on-screen keyboard, for a text field with no keyboard attached (compositor/src/osk.c).
keyboard_exe = executable('zacos9-keyboard',
  'keyboard.c',
  meson.project_source_root() / 'compositor/src/kblayout.c',
  client_protocol_src,
  include_directories: include_directories('../../compositor/src'),
  dependencies: [platinum_dep, wayland_client, xkbcommon_client],
  install: true,
)

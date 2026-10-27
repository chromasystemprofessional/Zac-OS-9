# Keeping screens awake for apps that ask over D-Bus (docs/energy-saver.md).
inhibit_exe = executable('zacos9-inhibit',
  'inhibit.c',
  finder_protocol,
  dependencies: [wayland_client, gio],
  install: true,
)

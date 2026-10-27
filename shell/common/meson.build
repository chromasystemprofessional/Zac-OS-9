# libzacos9-shell: what nearly every desktop program uses, built once. The
# window hints a program gives zacos9-wm (platinum-shell-v1: frame styles,
# the proxy icon, pop-up windows...), the Platinum alert box, and Shut Down
# and Restart from outside a session (the login window, the installer), and
# running a program and waiting for it without freezing the window (process.h).
shell_common = static_library('zacos9-shell',
  'platinumshell.cpp', 'alert.cpp', 'power.cpp', 'process.cpp', finder_protocol,
  dependencies: [platinum_dep, qt6, wayland_client],
)
shell_common_dep = declare_dependency(link_with: shell_common,
  include_directories: include_directories('.'),
  dependencies: [platinum_dep, qt6, wayland_client],
)

test('system-power',
  executable('test-system-power', '../../tests/session/test_power.cpp',
    dependencies: [shell_common_dep, qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
)

test('process',
  executable('test-process', '../../tests/common/test_process.cpp',
    dependencies: [shell_common_dep, qt6],
  ),
  timeout: 30,
)

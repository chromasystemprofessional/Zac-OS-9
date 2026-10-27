screenshare_host_src = ['host.cpp', 'common.cpp']
screenshare_host = executable('zacos9-screenshare-host', screenshare_host_src,
  link_with: panelkit,
  include_directories: include_directories('../finder', '../panels'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client, pam],
  install: true,
)
# The same, taking "sesame" instead of asking PAM, for the sound's login.
test_screenshare_host = executable('test-screenshare-host', screenshare_host_src,
  link_with: panelkit,
  include_directories: include_directories('../finder', '../panels'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client, pam],
  cpp_args: ['-DZACOS9_SCREENSHARE_TEST'],
)

# The viewer's side of the sound, on its own.
test_screenshare_sound = executable('test-screenshare-sound', '../../tests/screenshare/sound_client.cpp',
  'sound.cpp', 'common.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../panels'),
  dependencies: [shell_common_dep, platinum_dep, qt6],
)

test('screenshare-host', find_program('python3'),
  args: [files('../../tests/screenshare/test_host.py'), test_screenshare_host, test_screenshare_sound],
  timeout: 90,
)

# The other side: another computer's screen here, full screen, at this
# screen's own pixels whatever its scale (screenshare/rfb.cpp, viewer.cpp).
zlib = dependency('zlib')
screenshare_viewer = executable('zacos9-screenshare-viewer',
  'viewer.cpp',
  'rfb.cpp',
  'sound.cpp',
  'common.cpp',
  '../network/logindialog.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../panels', '../network'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client, zlib],
  install: true,
)
test('screenshare-rfb',
  executable('test-screenshare-rfb', '../../tests/screenshare/test_rfb.cpp',
    'rfb.cpp', 'common.cpp',
    link_with: panelkit,
    include_directories: include_directories('../finder', '../panels'),
    dependencies: [shell_common_dep, platinum_dep, qt6, zlib],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
)
# Screen Sharing's viewer against a real wayvnc, the guest at 1.5x.
test('screenshare-viewer', find_program('python3'),
  args: [files('../../tests/screenshare/test_viewer.py'), compositor_exe, screenshare_viewer, idle_client,
    vptr_exe, rawshot_exe],
  timeout: 120,
)

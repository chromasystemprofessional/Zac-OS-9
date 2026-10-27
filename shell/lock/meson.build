# The lock screen (docs/energy-saver.md): ext-session-lock-v1, PAM.
lock_protocol = []
foreach kind : [['client-header', '@BASENAME@-client-protocol.h'], ['private-code', '@BASENAME@-protocol.c']]
  lock_protocol += custom_target('lock_' + kind[0].underscorify(),
    input: wl_protocol_dir / 'staging/ext-session-lock/ext-session-lock-v1.xml',
    output: kind[1],
    command: [wayland_scanner, kind[0], '@INPUT@', '@OUTPUT@'],
  )
endforeach
executable('zacos9-lock', 'lock.c', lock_protocol, client_protocol_src,
  dependencies: [platinum_dep, wayland_client, xkbcommon_client, pam],
  install: true,
)
# The same, taking "sesame" instead of asking PAM: tests/compositor/test_lock.py.
test_lock_exe = executable('test-lock', 'lock.c', lock_protocol, client_protocol_src,
  c_args: ['-DZACOS9_LOCK_TEST'],
  dependencies: [platinum_dep, wayland_client, xkbcommon_client],
)

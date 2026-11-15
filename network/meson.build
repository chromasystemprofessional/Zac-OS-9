# Connecting to other computers: our own AFP client, for classic Macs
# (nothing in Debian speaks AFP 2).
fuse3 = dependency('fuse3')
gcrypt = dependency('libgcrypt')
threads = dependency('threads')

afp_client = executable('zacos9-afp',
  'afp/main.c',
  'afp/afp.c',
  'afp/afpfs.c',
  'afp/dsi.c',
  'afp/asp.c',
  dependencies: [fuse3, gcrypt, threads],
  install: true,
)

# A private bus: the running session's gvfs probes the FUSE mount the test
# makes, and then the test's own eject finds it busy.
test('afp-disconnect', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(),
    files('../tests/sharing/test_afp_disconnect.py'), afp_client.full_path()],
  depends: [afp_client],
  timeout: 90,
)

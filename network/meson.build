# Connecting to other computers: our own AFP client, for classic Macs
# (nothing in Debian speaks AFP 2).
fuse3 = dependency('fuse3')
gcrypt = dependency('libgcrypt')
threads = dependency('threads')

# The reply parsers are pure (bytes in, structs out), so the unit test and
# the fuzzer can exercise them without a server, a socket or gcrypt.
afp_parse = static_library('afp-parse',
  'afp/afp_parse.c',
)

afp_client = executable('zacos9-afp',
  'afp/main.c',
  'afp/afp.c',
  'afp/afpfs.c',
  'afp/dsi.c',
  'afp/asp.c',
  dependencies: [fuse3, gcrypt, threads],
  link_with: afp_parse,
  install: true,
)

test('afp-parse', executable('test-afp-parse',
  '../tests/security/test_afp_parse.c',
  link_with: afp_parse,
  include_directories: include_directories('afp'),
  build_by_default: false,
), timeout: 60)

if get_option('fuzz')
  fuzz_afp = executable('fuzz-afp-parse',
    '../tests/security/fuzz_afp_parse.c',
    link_with: afp_parse,
    include_directories: include_directories('afp'),
    c_args: ['-fsanitize=fuzzer,address,undefined', '-fno-omit-frame-pointer'],
    link_args: ['-fsanitize=fuzzer,address,undefined'],
    build_by_default: false,
  )
  test('fuzz-afp-parse', fuzz_afp, args: ['-runs=2000'], timeout: 120)
endif

# A private bus: the running session's gvfs probes the FUSE mount the test
# makes, and then the test's own eject finds it busy.
test('afp-disconnect', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(),
    files('../tests/sharing/test_afp_disconnect.py'), afp_client.full_path()],
  depends: [afp_client],
  timeout: 90,
)

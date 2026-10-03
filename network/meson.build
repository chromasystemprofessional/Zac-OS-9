# Connecting to other computers: our own AFP client, for classic Macs
# (nothing in Debian speaks AFP 2).
fuse3 = dependency('fuse3')
gcrypt = dependency('libgcrypt')
threads = dependency('threads')

executable('zacos9-afp',
  'afp/main.c',
  'afp/afp.c',
  'afp/afpfs.c',
  'afp/dsi.c',
  dependencies: [fuse3, gcrypt, threads],
  install: true,
)

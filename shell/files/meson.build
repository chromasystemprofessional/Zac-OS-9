finder_fonts = static_library('finder-fonts', 'fonts.cpp',
  include_directories: include_directories('../common'),
  dependencies: [platinum_dep, qt6, dependency('fontconfig'),
    dependency('freetype2'), dependency('harfbuzz')],
)
finder_fonts_dep = declare_dependency(link_with: finder_fonts,
  include_directories: include_directories('.'),
  dependencies: [platinum_dep, qt6])

# libzacos9-files: the Sniffer's model of files, which the file chooser,
# Disk Setup, Software, the printer windows and others share: items and their
# icons, local and network volumes and when to mount them, the Macintosh view,
# aliases, clippings, the Applications database and Trash, extensions and
# Extensions Manager sets, desktop printers, file associations, thumbnails
# and resource forks.
shell_files = static_library('zacos9-files',
  'alias.cpp', 'localvolumes.cpp', 'mountpolicy.cpp',
  'netvolumes.cpp', 'smbclient.cpp', 'vfs.cpp',
  'resources.cpp', 'autostart.cpp', 'extensioncatalog.cpp',
  'appdb.cpp', 'apptrash.cpp', 'items.cpp', 'clippings.cpp',
  'desktopprinter.cpp', 'printers.cpp', 'thumbnails.cpp',
  'fileassoc.cpp', 'tricksets.cpp',
  include_directories: include_directories('../panels'),
  dependencies: [shell_common_dep, platinum_dep, qt6, gio, gio_unix, wayland_client, finder_fonts_dep],
)
shell_files_dep = declare_dependency(link_with: [shell_files, panelkit],
  include_directories: include_directories('.'),
  dependencies: [shell_common_dep, gio, gio_unix, finder_fonts_dep],
)

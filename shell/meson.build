wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor')
xkbcommon_client = dependency('xkbcommon')
wayland_protos = dependency('wayland-protocols')
wayland_scanner = find_program(
  dependency('wayland-scanner', native: true).get_variable('wayland_scanner'),
  native: true,
)
wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')

client_protocols = [
  wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml', # layer-shell popups
  wl_protocol_dir / 'stable/viewporter/viewporter.xml',
  wl_protocol_dir / 'staging/fractional-scale/fractional-scale-v1.xml', # 1.25x, 1.5x, 1.75x
  meson.project_source_root() / 'protocols/wlr-layer-shell-unstable-v1.xml',
  meson.project_source_root() / 'protocols/wlr-foreign-toplevel-management-unstable-v1.xml',
  meson.project_source_root() / 'protocols/virtual-keyboard-unstable-v1.xml',
]
client_protocol_src = []
foreach xml : client_protocols
  name = xml.underscorify()
  client_protocol_src += custom_target(name + '_client_h',
    input: xml,
    output: '@BASENAME@-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  client_protocol_src += custom_target(name + '_c',
    input: xml,
    output: '@BASENAME@-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
endforeach

# Expanding archives on a double-click (the Finder runs it from beside itself).
configure_file(input: 'finder/zacos9-expand', output: 'zacos9-expand', copy: true)
install_data('finder/zacos9-expand', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
test('expand', find_program('python3'),
  args: [files('../tests/finder/test_expand.py'), files('finder/zacos9-expand')],
  # About 9 s on an idle machine; a busy package build needs more.
  timeout: 120)

configure_file(input: 'finder/zacos9-mac-disks', output: 'zacos9-mac-disks', copy: true)
install_data('finder/zacos9-mac-disks', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
test('mac-disks', find_program('python3'),
  args: [files('../tests/finder/test_mac_disks.py'), files('finder/zacos9-mac-disks'),
    files('../disks/zacos9-mac-mount')])

# Screen Snapshot (Apple menu): drag out a rectangle; a picture on the
# Desktop and the clipboard (slurp, native committed-frame capture, wl-copy).
configure_file(input: 'snapshot/zacos9-snapshot', output: 'zacos9-snapshot', copy: true)
install_data('snapshot/zacos9-snapshot', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')

# The classic Mac OS launcher, next to the Finder that runs it.
configure_file(input: 'classic/zacos9-classic', output: 'zacos9-classic', copy: true)
install_data('classic/zacos9-classic', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
test('classic-window-probe', find_program('python3'),
  args: [files('../tests/emulation/test_classic_probe.py')],
  env: ['PYTHONDONTWRITEBYTECODE=1'],
)

# The Wine launcher for Windows applications, next to the Finder.
configure_file(input: 'wine/zacos9-wine', output: 'zacos9-wine', copy: true)
install_data('wine/zacos9-wine', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')

# GDBus: the menu bar's global menus (registrar, dbusmenu); GIO for the
# Finder's desktop entries too.
gio = dependency('gio-2.0')

test('erase-disk-menu',
  executable('test-erase-menu', 'menubar/test_erase_menu.c',
    dependencies: [platinum_dep, wayland_client],
    c_args: ['-ffunction-sections', '-fdata-sections'],
    link_args: ['-Wl,--gc-sections']))

# platinum-shell-v1: the menu bar (front window's pid) and the Finder.
finder_protocol = []
foreach kind : [['client-header', '@BASENAME@-client-protocol.h'], ['private-code', '@BASENAME@-protocol.c']]
  finder_protocol += custom_target('finder_platinum_shell_' + kind[0].underscorify(),
    input: meson.project_source_root() / 'protocols/platinum-shell-v1.xml',
    output: kind[1],
    command: [wayland_scanner, kind[0], '@INPUT@', '@OUTPUT@'],
  )
endforeach

menubar_exe = executable('zacos9-menubar',
  'menubar/main.c',
  'menubar/menus.c',
  'menubar/toplevels.c',
  'menubar/launch.c',
  'menubar/keys.c',
  'menubar/finderlink.c',
  'menubar/appmenu.c',
  'menubar/logout.c',
  'menubar/recent.c',
  'menubar/kbmenu.c',
  'menubar/appicon.c',
  'menubar/hints.c',
  client_protocol_src,
  finder_protocol,
  dependencies: [platinum_dep, wayland_client, wayland_cursor, xkbcommon_client, gio,
    dependency('gdk-pixbuf-2.0'), appfilter_dep],
  install: true,
)

# Hint Bubbles over the menu bar's menus (menubar/hints.c).
test('menu-hints',
  executable('test-hints', 'menubar/test_hints.c', 'menubar/hints.c',
    dependencies: [platinum_dep, wayland_client],
  ),
  timeout: 10,
)

# The Zac menu's recent items (menubar/recent.c).
test('menu-recent',
  executable('test-recent', 'menubar/test_recent.c', 'menubar/recent.c',
    dependencies: [platinum_dep, gio],
  ),
  timeout: 10,
)

# The keyboard menu (menubar/kbmenu.c).
test('menu-keyboard',
  executable('test-kbmenu', 'menubar/test_kbmenu.c', 'menubar/kbmenu.c',
    dependencies: [platinum_dep, wayland_client],
  ),
  timeout: 10,
)

# ⌘-Tab's choice of program (menubar/toplevels.c).
test('menu-app-switch',
  executable('test-app-switch',
    'menubar/test_app_switch.c', 'menubar/toplevels.c', client_protocol_src,
    dependencies: [platinum_dep, wayland_client],
  ),
  timeout: 10,
)

# The lock screen (docs/energy-saver.md): ext-session-lock-v1, PAM.
lock_protocol = []
foreach kind : [['client-header', '@BASENAME@-client-protocol.h'], ['private-code', '@BASENAME@-protocol.c']]
  lock_protocol += custom_target('lock_' + kind[0].underscorify(),
    input: wl_protocol_dir / 'staging/ext-session-lock/ext-session-lock-v1.xml',
    output: kind[1],
    command: [wayland_scanner, kind[0], '@INPUT@', '@OUTPUT@'],
  )
endforeach
# libpam directly: Debian's pam.pc also needs libaudit's, for nothing here.
pam = meson.get_compiler('c').find_library('pam', has_headers: ['security/pam_appl.h'])
executable('zacos9-lock', 'lock/lock.c', lock_protocol, client_protocol_src,
  dependencies: [platinum_dep, wayland_client, xkbcommon_client, pam],
  install: true,
)
# The same, taking "sesame" instead of asking PAM: compositor/tests/test_lock.py.
test_lock_exe = executable('test-lock', 'lock/lock.c', lock_protocol, client_protocol_src,
  c_args: ['-DZACOS9_LOCK_TEST'],
  dependencies: [platinum_dep, wayland_client, xkbcommon_client],
)

# The screen saver (docs/energy-saver.md).
saver_exe = executable('zacos9-screensaver',
  'screensaver/screensaver.c',
  client_protocol_src,
  dependencies: [platinum_dep, wayland_client],
  install: true,
)

# The on-screen keyboard, for a text field with no keyboard attached (compositor/src/osk.c).
keyboard_exe = executable('zacos9-keyboard',
  'keyboard/keyboard.c',
  meson.project_source_root() / 'compositor/src/kblayout.c',
  client_protocol_src,
  include_directories: include_directories('../compositor/src'),
  dependencies: [platinum_dep, wayland_client, xkbcommon_client],
  install: true,
)

# Keeping screens awake for apps that ask over D-Bus (docs/energy-saver.md).
inhibit_exe = executable('zacos9-inhibit',
  'inhibit/inhibit.c',
  finder_protocol,
  dependencies: [wayland_client, gio],
  install: true,
)

test('menu-launch-feedback',
  executable('test-menu-launch',
    'menubar/test_launch.c', 'menubar/launch.c',
    dependencies: [platinum_dep, wayland_client, gio, appfilter_dep],
  ),
  timeout: 10,
)

# The Finder: Qt6 for windows, input and files; all drawing through lib/.
add_languages('cpp', native: false)
qt6 = dependency('qt6', modules: ['Core', 'Gui', 'Widgets', 'Network', 'DBus'],
  private_headers: true) # QPlatformNativeInterface: a window's wl_surface
snapshot_protocol = []
foreach kind : [['client-header', '@BASENAME@-client-protocol.h'], ['private-code', '@BASENAME@-protocol.c']]
  snapshot_protocol += custom_target('zacos_snapshot_client_' + kind[0].underscorify(),
    input: meson.project_source_root() / 'protocols/zacos-snapshot-v1.xml',
    output: kind[1],
    command: [wayland_scanner, kind[0], '@INPUT@', '@OUTPUT@'],
  )
endforeach
capture_exe = executable('zacos9-capture', 'snapshot/capture.cpp',
  'finder/alert.cpp', 'finder/platinumshell.cpp', finder_protocol, snapshot_protocol,
  dependencies: [platinum_dep, qt6, wayland_client],
  include_directories: include_directories('finder'),
  install: true,
)
test('snapshot-image',
  executable('test-snapshot-image', '../tests/finder/test_snapshot_image.cpp',
    'finder/alert.cpp', 'finder/platinumshell.cpp', finder_protocol, snapshot_protocol,
    dependencies: [platinum_dep, qt6, wayland_client],
    include_directories: include_directories('finder')),
  env: ['QT_QPA_PLATFORM=offscreen'],
)
snapshot_animation = executable('test-snapshot-animation',
  '../tests/finder/snapshot_animation.cpp',
  dependencies: [qt6])
test('native-snapshot', find_program('python3'),
  args: [files('../tests/finder/test_snapshot.py'), compositor_exe, capture_exe,
    files('snapshot/zacos9-snapshot'), snapshot_animation],
  timeout: 60,
)
layershellqt = dependency('LayerShellQt', method: 'cmake',
  modules: ['LayerShellQt::Interface'])
# GIO reads and launches XDG desktop entries for the Applications folder,
# so the desktop entry specification is followed rather than approximated.
# With it, Limited accounts' program filter (lib/appfilter.c): every
# program that lists or launches applications through GIO uses both.
gio_unix = [dependency('gio-unix-2.0'), appfilter_dep]

finder_fonts = static_library('finder-fonts', 'finder/fonts.cpp',
  dependencies: [platinum_dep, qt6, dependency('fontconfig'),
    dependency('freetype2'), dependency('harfbuzz')],
)
finder_fonts_dep = declare_dependency(link_with: finder_fonts,
  dependencies: [platinum_dep, qt6])

executable('zacos9-autostart',
  'finder/autostart_main.cpp',
  'finder/autostart.cpp',
  dependencies: [qt6, gio, gio_unix, platinum_dep],
  install: true,
)

# Control panels (the Finder's Get Info uses their controls too).
panelkit = static_library('panelkit', 'panels/panelkit.cpp', 'panels/sharingclient.cpp',
  'panels/custompatterns.cpp',
  'panels/customthemes.cpp',
  include_directories: include_directories('finder'),
  dependencies: [platinum_dep, qt6],
)

# The chooser shows the startup disk through the Finder's Macintosh view.
file_chooser_sources = [
  'finder/filechooserportal.cpp',
  'finder/alias.cpp',
  'finder/localvolumes.cpp', 'finder/mountpolicy.cpp', 'network/netvolumes.cpp', 'network/smbclient.cpp',
  'finder/vfs.cpp', 'finder/resources.cpp', 'finder/autostart.cpp',
  'finder/extensioncatalog.cpp', 'finder/appdb.cpp', 'finder/apptrash.cpp',
  'finder/platinumshell.cpp', finder_protocol, 'finder/items.cpp', 'finder/clippings.cpp', 'print/desktopprinter.cpp', 'print/printers.cpp', 'finder/thumbnails.cpp', 'finder/fileassoc.cpp',
]
file_chooser_deps = [platinum_dep, qt6, gio, gio_unix, wayland_client, finder_fonts_dep]

file_portal = executable('zacos9-file-portal',
  'finder/filechooserportal_main.cpp',
  file_chooser_sources,
  link_with: panelkit,
  include_directories: include_directories('finder', 'print', 'network', 'panels'),
  dependencies: file_chooser_deps,
  install: true,
)

test('filechooser-locations',
  executable('test-filechooser-portal',
    'finder/tests/test_filechooser_portal.cpp',
    file_chooser_sources,
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'network', 'panels'),
    dependencies: file_chooser_deps,
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
)

test('filechooser-requests', find_program('dbus-run-session'),
  args: [executable('test-filechooser-requests',
    'finder/tests/test_filechooser_requests.cpp', file_chooser_sources,
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'network', 'panels'),
    dependencies: file_chooser_deps,
  )],
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 45,
)

test('filechooser-lifecycle', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(),
    files('../tests/finder/test_filechooser_lifecycle.py'), file_portal.full_path()],
  depends: [file_portal],
  timeout: 30,
)

configure_file(input: 'panels/zacos9-patterns', output: 'zacos9-patterns', copy: true)
configure_file(input: 'panels/zacos9-soundthemes', output: 'zacos9-soundthemes', copy: true)
install_data('panels/zacos9-soundthemes', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
test('sound-themes', find_program('python3'),
  args: [files('../tests/panels/test_soundthemes.py'), files('panels/zacos9-soundthemes')],
  timeout: 60,
)
test('custom-themes',
  executable('test-customthemes', '../tests/panels/test_customthemes.cpp', 'panels/appearance.cpp',
    link_with: panelkit,
    include_directories: include_directories('panels', 'finder'),
    dependencies: [platinum_dep, qt6],
  ),
  args: [meson.project_source_root() / 'assets/sounds/woodblock.wav'],
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)
install_data('panels/zacos9-patterns', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
test('custom-patterns', find_program('python3'),
  args: [files('../tests/panels/test_patterns.py'), files('panels/zacos9-patterns')],
  timeout: 60,
)
test('custom-pattern-catalog',
  executable('test-custompatterns', '../tests/panels/test_custompatterns.cpp', 'panels/appearance.cpp',
    link_with: panelkit,
    include_directories: include_directories('panels', 'finder'),
    dependencies: [platinum_dep, qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 30,
)

finder_exe = executable('zacos9-finder',
  'finder/main.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  'finder/desktop.cpp',
  'finder/secondarydesktop.cpp',
  'finder/folderwindow.cpp',
  'finder/viewoptions.cpp',
  'finder/viewoptionsdialog.cpp',
  'finder/tinyicons.cpp',
  'finder/toybox.cpp',
  'finder/items.cpp', 'finder/clippings.cpp', 'print/desktopprinter.cpp', 'print/printers.cpp', 'finder/thumbnails.cpp', 'finder/fileassoc.cpp',
  'finder/finder.cpp', 'finder/folderpath.cpp', 'finder/cryptofiles.cpp',
  'finder/foldertricks.cpp', 'finder/contextitems.cpp', 'finder/printwindow.cpp',
  'store/storeclient.cpp',
  'finder/fileops.cpp',
  'finder/alias.cpp',
  'finder/autostart.cpp',
  'finder/userservices.cpp',
  'finder/transferdialog.cpp',
  'finder/labeleditor.cpp',
  'finder/infowindow.cpp', 'finder/gpuinfo.cpp', 'finder/modules.cpp',
  'finder/findwindow.cpp', 'finder/findcriteria.cpp', 'finder/findindex.cpp',
  'finder/folderstate.cpp',
  'finder/vfs.cpp',
  'finder/resources.cpp',
  'finder/extensioncatalog.cpp',
  'finder/resourcewindows.cpp',
  'finder/tricksets.cpp',
  'finder/appdb.cpp',
  'finder/apptrash.cpp',
  'finder/localvolumes.cpp', 'finder/mountpolicy.cpp',
  'finder/diskinit.cpp',
  'finder/filemanager1.cpp',
  'network/netvolumes.cpp',
  'network/smbclient.cpp',
  link_with: panelkit,
  include_directories: include_directories('finder', 'print', 'panels', 'network'),
  dependencies: [platinum_dep, qt6, layershellqt, wayland_client, gio, gio_unix, finder_fonts_dep],
  cpp_args: ['-DZACOS9_DISK_HELPER="@0@"'.format(
    get_option('prefix') / get_option('libexecdir') / 'zacos9' / 'zacos9-disk-helper')],
  install: true,
)

# The Macintosh view of this computer: the curated hierarchy, and the
# Applications folder built from XDG desktop entries. The test builds a
# private XDG environment, so it reads only its own desktop entries.
finder_test_sources = [
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  'finder/desktop.cpp',
  'finder/secondarydesktop.cpp',
  'finder/folderwindow.cpp',
  'finder/viewoptions.cpp',
  'finder/viewoptionsdialog.cpp',
  'finder/tinyicons.cpp',
  'finder/toybox.cpp',
  'finder/items.cpp', 'finder/clippings.cpp', 'print/desktopprinter.cpp', 'print/printers.cpp', 'finder/thumbnails.cpp', 'finder/fileassoc.cpp',
  'finder/finder.cpp', 'finder/folderpath.cpp', 'finder/cryptofiles.cpp',
  'finder/foldertricks.cpp', 'finder/contextitems.cpp', 'finder/printwindow.cpp',
  'store/storeclient.cpp',
  'finder/fileops.cpp',
  'finder/alias.cpp',
  'finder/autostart.cpp',
  'finder/userservices.cpp',
  'finder/transferdialog.cpp',
  'finder/labeleditor.cpp',
  'finder/infowindow.cpp', 'finder/gpuinfo.cpp', 'finder/modules.cpp',
  'finder/findwindow.cpp', 'finder/findcriteria.cpp', 'finder/findindex.cpp',
  'finder/folderstate.cpp',
  'finder/vfs.cpp',
  'finder/resources.cpp',
  'finder/extensioncatalog.cpp',
  'finder/resourcewindows.cpp',
  'finder/tricksets.cpp',
  'finder/appdb.cpp',
  'finder/apptrash.cpp',
  'finder/diskinit.cpp',
  'finder/filemanager1.cpp',
  'network/netvolumes.cpp',
  'network/smbclient.cpp',
]
test('finder-selection',
  executable('test-finder-selection', '../tests/finder/test_selection.cpp', finder_test_sources,
    'finder/localvolumes.cpp', 'finder/mountpolicy.cpp',
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'panels', 'network'),
    dependencies: [platinum_dep, qt6, layershellqt, wayland_client, gio, gio_unix, finder_fonts_dep],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)
test('finder-fonts',
  executable('test-finder-fonts', '../tests/finder/test_fonts.cpp', finder_test_sources,
    'finder/localvolumes.cpp', 'finder/mountpolicy.cpp',
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'panels', 'network'),
    dependencies: [platinum_dep, qt6, layershellqt, wayland_client, gio, gio_unix, finder_fonts_dep],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)
test('finder-folder-tricks',
  executable('test-finder-folder-tricks', '../tests/finder/test_foldertricks.cpp',
    'finder/foldertricks.cpp', 'finder/contextitems.cpp',
    include_directories: include_directories('finder'),
    dependencies: [qt6, gio, gio_unix],
  ),
  timeout: 60,
)
test('finder-print-window',
  executable('test-finder-print-window', '../tests/finder/test_printwindow.cpp',
    'finder/printwindow.cpp', 'finder/platinumshell.cpp', finder_protocol,
    link_with: panelkit,
    include_directories: include_directories('finder', 'panels'),
    dependencies: [platinum_dep, qt6, wayland_client],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 30,
)
test('finder-crypto',
  executable('test-finder-crypto', '../tests/finder/test_crypto.cpp',
    'finder/cryptofiles.cpp',
    include_directories: include_directories('finder'),
    dependencies: [qt6],
  ),
  timeout: 30,
)
# Find: criteria, the search index, Items Found (findcriteria.cpp, findindex.cpp, findwindow.cpp).
test('finder-find',
  executable('test-finder-find', '../tests/finder/test_find.cpp', finder_test_sources,
    'finder/localvolumes.cpp', 'finder/mountpolicy.cpp',
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'panels', 'network'),
    dependencies: [platinum_dep, qt6, layershellqt, wayland_client, gio, gio_unix, finder_fonts_dep],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)
# Clippings, Internet locations and Show Clipboard (clippings.cpp).
test('finder-clippings',
  executable('test-finder-clippings', '../tests/finder/test_clippings.cpp', finder_test_sources,
    'finder/localvolumes.cpp', 'finder/mountpolicy.cpp',
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'panels', 'network'),
    dependencies: [platinum_dep, qt6, layershellqt, wayland_client, gio, gio_unix, finder_fonts_dep],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)
# The Toy Box (toybox.cpp).
test('finder-toy-box',
  executable('test-finder-toy-box', '../tests/finder/test_toybox.cpp',
    finder_test_sources, 'finder/localvolumes.cpp', 'finder/mountpolicy.cpp',
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'panels', 'network'),
    dependencies: [platinum_dep, qt6, layershellqt, wayland_client, gio, gio_unix, finder_fonts_dep],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)
# View Options and Preferences (viewoptions.cpp, folderwindow.cpp).
test('finder-view-options',
  executable('test-finder-view-options', '../tests/finder/test_view_options.cpp',
    finder_test_sources, 'finder/localvolumes.cpp', 'finder/mountpolicy.cpp',
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'panels', 'network'),
    dependencies: [platinum_dep, qt6, layershellqt, wayland_client, gio, gio_unix, finder_fonts_dep],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)
test('finder-thumbnails',
  executable('test-finder-thumbnails', '../tests/finder/test_thumbnails.cpp',
    finder_test_sources, 'finder/localvolumes.cpp', 'finder/mountpolicy.cpp',
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'panels', 'network'),
    dependencies: [platinum_dep, qt6, layershellqt, wayland_client, gio, gio_unix, finder_fonts_dep],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)
test('finder-fileassoc',
  executable('test-finder-fileassoc', '../tests/finder/test_fileassoc.cpp',
    finder_test_sources, 'finder/localvolumes.cpp', 'finder/mountpolicy.cpp',
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'panels', 'network'),
    dependencies: [platinum_dep, qt6, layershellqt, wayland_client, gio, gio_unix, finder_fonts_dep],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)
test('finder-gpu-info',
  executable('test-finder-gpu-info', '../tests/finder/test_gpuinfo.cpp',
    'finder/gpuinfo.cpp',
    include_directories: include_directories('finder'),
    dependencies: [qt6],
  ),
  timeout: 30,
)
test('finder-vfs',
  executable('test-finder-vfs',
    'finder/tests/test_vfs.cpp',
    'finder/vfs.cpp',
    'finder/folderpath.cpp',
    'finder/resources.cpp',
    'finder/autostart.cpp',
    'finder/extensioncatalog.cpp',
    'finder/appdb.cpp',
    'finder/apptrash.cpp',
    'finder/platinumshell.cpp',
    finder_protocol,
    'finder/items.cpp', 'finder/clippings.cpp', 'print/desktopprinter.cpp', 'print/printers.cpp', 'finder/thumbnails.cpp', 'finder/fileassoc.cpp',
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'panels'),
    dependencies: [platinum_dep, qt6, gio, gio_unix, wayland_client, finder_fonts_dep],
    cpp_args: ['-DZACOS9_RESOURCE_TESTING'],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 180,
)

test('autostart',
  executable('test-autostart', 'finder/tests/test_autostart.cpp', 'finder/autostart.cpp',
    include_directories: include_directories('finder'),
    dependencies: [qt6, gio, gio_unix, platinum_dep],
  ),
)

test('alias',
  executable('test-alias', 'finder/tests/test_alias.cpp', 'finder/alias.cpp',
    include_directories: include_directories('finder'),
    dependencies: [qt6],
  ),
)

test('userservices',
  executable('test-userservices', 'finder/tests/test_userservices.cpp', 'finder/userservices.cpp',
    include_directories: include_directories('finder'),
    dependencies: [qt6],
  ),
)

test('extension-catalog',
  executable('test-extension-catalog', 'finder/tests/test_extensioncatalog.cpp',
    'finder/extensioncatalog.cpp',
    include_directories: include_directories('finder'),
    dependencies: [platinum_dep, qt6],
  ),
)

test('disk-initialization-ui',
  executable('test-diskinit', '../tests/finder/test_diskinit.cpp',
    'finder/diskinit.cpp', 'finder/localvolumes.cpp', 'finder/mountpolicy.cpp',
    'finder/alert.cpp', 'finder/platinumshell.cpp', finder_protocol,
    dependencies: [platinum_dep, qt6, gio, wayland_client],
    include_directories: include_directories('finder', 'print', 'network', 'panels'),
    link_with: panelkit,
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)

test('local-volume-hotplug',
  executable('test-localvolumes', '../tests/finder/test_localvolumes.cpp',
    'finder/localvolumes.cpp', 'finder/mountpolicy.cpp',
    dependencies: [platinum_dep, qt6, gio],
    include_directories: include_directories('finder', 'print', 'network'),
  ),
  env: ['QT_NO_GLIB=1'],
  timeout: 10,
)

executable('zacos9-appearance',
  'panels/main.cpp',
  'panels/appearance.cpp',
  link_with: panelkit,
  include_directories: include_directories('finder'), # pixels.h
  dependencies: [platinum_dep, qt6],
  install: true,
)

executable('zacos9-collarpanel',
  'panels/collarpanel_main.cpp',
  'panels/collarpanel.cpp',
  'collar/collarart.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'print', 'collar'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)

executable('zacos9-datetime',
  'panels/datetime_main.cpp',
  'panels/datetime.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)

# File Exchange: which application opens each file type (GIO, the
# user's mimeapps.list).
executable('zacos9-fileexchange',
  'panels/fileexchange_main.cpp',
  'panels/fileexchange.cpp',
  'finder/fileassoc.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder'),
  dependencies: [platinum_dep, qt6, wayland_client, gio, gio_unix],
  install: true,
)

executable('zacos9-tcpip',
  'panels/tcpip.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)

# Bluetooth: BlueZ over the system D-Bus; headsets and speakers become the
# sound output (pactl).
executable('zacos9-bluetooth',
  'panels/bluetooth.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)

test('tricks-sets',
  executable('test-tricks-sets', 'finder/tests/test_tricksets.cpp', 'finder/tricksets.cpp',
    include_directories: include_directories('finder'),
    dependencies: [platinum_dep, qt6],
  ),
  timeout: 10,
)

# zacos9-finder-ctl: scripting the Sniffer over its socket.
finderctl_exe = executable('zacos9-finder-ctl', 'finderctl/main.c',
  dependencies: [platinum_dep],
  install: true,
)
test('finder-ctl', find_program('python3'),
  args: [files('finderctl/test_finderctl.py'), finderctl_exe.full_path()],
  depends: finderctl_exe, timeout: 20)

# Disk Setup: First Aid (verify, repair, drive health) and partitioning,
# all through UDisks2 (disksetup/); the installer opens it too.
disksetup_exe = executable('zacos9-disksetup',
  'disksetup/main.cpp', 'disksetup/diskmap.cpp', 'disksetup/udisks.cpp', 'disksetup/plan.cpp',
  'disksetup/inuse.cpp',
  'finder/mountpolicy.cpp', 'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels', 'disksetup'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)

test('disksetup-plan',
  executable('test-disksetup-plan', 'disksetup/test_plan.cpp', 'disksetup/plan.cpp', 'disksetup/udisks.cpp',
    dependencies: [qt6],
  ),
  env: ['LC_ALL=en_US.UTF-8'], timeout: 10,
)
# The partition map against a stand-in UDisks: drag, +, Apply (vptr is defined further down).
disksetup_map_test = files('../tests/disksetup/test_partition_map.py')
test('disksetup-udisks',
  executable('test-disksetup-udisks', 'disksetup/test_udisks.cpp', 'disksetup/udisks.cpp',
    'disksetup/inuse.cpp', 'finder/mountpolicy.cpp',
    include_directories: include_directories('finder'),
    dependencies: [qt6],
  ),
  env: ['LC_ALL=en_US.UTF-8'], timeout: 10,
)

# Walkies: places, each with its network, sound, time zone, printer and
# startup set (walkies/location.cpp).
executable('zacos9-walkies',
  'walkies/main.cpp', 'walkies/location.cpp',
  'finder/tricksets.cpp', 'finder/autostart.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels', 'walkies'),
  dependencies: [platinum_dep, qt6, wayland_client, gio, gio_unix],
  install: true,
)
test('walkies-locations',
  executable('test-walkies-locations', 'walkies/test_location.cpp', 'walkies/location.cpp',
    include_directories: include_directories('walkies'),
    dependencies: [platinum_dep, qt6],
  ),
  timeout: 10,
)

# Jigsaw Puzzle: a picture in pieces.
executable('zacos9-jigsaw',
  'jigsaw/main.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  include_directories: include_directories('finder'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)

# Mouse Practice: Learn ZacOS's first lesson (mousepractice/practice.cpp
# plays it; docs/tutorial.md).
executable('zacos9-mousepractice',
  'mousepractice/main.cpp', 'mousepractice/practice.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  include_directories: include_directories('finder', 'mousepractice'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)
test('mouse-practice',
  executable('test-mouse-practice', 'mousepractice/tests/test_practice.cpp', 'mousepractice/practice.cpp',
    include_directories: include_directories('mousepractice'),
    dependencies: [qt6],
  ),
  timeout: 10,
)

# Plotter: graphs of y = f(x) (plotter/expr.cpp reads them).
executable('zacos9-plotter',
  'plotter/main.cpp', 'plotter/expr.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels', 'plotter'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)
test('plotter-expr',
  executable('test-plotter-expr', 'plotter/test_expr.cpp', 'plotter/expr.cpp',
    dependencies: [qt6]),
  timeout: 10)

# Retriever: folder pairs kept alike with rsync (retriever/sync.cpp).
executable('zacos9-retriever',
  'retriever/main.cpp', 'retriever/sync.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels', 'retriever'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)
test('retriever-sync',
  executable('test-retriever-sync', 'retriever/test_sync.cpp', 'retriever/sync.cpp',
    dependencies: [qt6],
  ),
  timeout: 30,
)

# Keychain Access: the Secret Service's keychains and their items
# (collar/keychain.cpp, shared with The Collar's Keychain module).
executable('zacos9-keychain',
  'panels/keychainaccess.cpp',
  'collar/keychain.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'collar'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)

executable('zacos9-filesharing',
  'panels/filesharing.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)

# Screen Sharing: the panel, and the host it starts (wayvnc behind it).
executable('zacos9-screensharing',
  'panels/screensharing.cpp',
  'screenshare/common.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels', 'screenshare'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)

screenshare_host_src = ['screenshare/host.cpp', 'screenshare/common.cpp', 'finder/alert.cpp',
  'finder/platinumshell.cpp', finder_protocol]
screenshare_host = executable('zacos9-screenshare-host', screenshare_host_src,
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels', 'screenshare'),
  dependencies: [platinum_dep, qt6, wayland_client, pam],
  install: true,
)
# The same, taking "sesame" instead of asking PAM, for the sound's login.
test_screenshare_host = executable('test-screenshare-host', screenshare_host_src,
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels', 'screenshare'),
  dependencies: [platinum_dep, qt6, wayland_client, pam],
  cpp_args: ['-DZACOS9_SCREENSHARE_TEST'],
)

# The viewer's side of the sound, on its own.
test_screenshare_sound = executable('test-screenshare-sound', '../tests/screenshare/sound_client.cpp',
  'screenshare/sound.cpp', 'screenshare/common.cpp',
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels', 'screenshare'),
  dependencies: [platinum_dep, qt6],
)

test('screenshare-host', find_program('python3'),
  args: [files('../tests/screenshare/test_host.py'), test_screenshare_host, test_screenshare_sound],
  timeout: 90,
)

# The other side: another computer's screen here, full screen, at this
# screen's own pixels whatever its scale (screenshare/rfb.cpp, viewer.cpp).
zlib = dependency('zlib')
screenshare_viewer = executable('zacos9-screenshare-viewer',
  'screenshare/viewer.cpp',
  'screenshare/rfb.cpp',
  'screenshare/sound.cpp',
  'screenshare/common.cpp',
  'network/logindialog.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels', 'screenshare', 'network'),
  dependencies: [platinum_dep, qt6, wayland_client, zlib],
  install: true,
)
test('screenshare-rfb',
  executable('test-screenshare-rfb', '../tests/screenshare/test_rfb.cpp',
    'screenshare/rfb.cpp', 'screenshare/common.cpp',
    link_with: panelkit,
    include_directories: include_directories('finder', 'panels', 'screenshare'),
    dependencies: [platinum_dep, qt6, zlib],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
)

# Parsing avahi-browse and WS-Discovery replies, against recorded text
# (see the test file for what could and couldn't be checked live).
test('discovery',
  executable('test-discovery',
    'network/tests/test_discovery.cpp',
    'network/discovery.cpp',
    include_directories: include_directories('network'),
    dependencies: [platinum_dep, qt6],
  ),
  timeout: 60,
)

# AFP volume names and mount points (network/netvolumes.cpp).
test('netvolumes',
  executable('test-netvolumes',
    'network/tests/test_netvolumes.cpp',
    'network/netvolumes.cpp',
    'network/smbclient.cpp',
    include_directories: include_directories('network'),
    dependencies: [qt6],
  ),
)

executable('zacos9-netbrowser',
  'network/main.cpp',
  'network/netbrowser.cpp',
  'network/connectdialog.cpp',
  'network/logindialog.cpp',
  'network/volumedialog.cpp',
  'network/discovery.cpp',
  'network/afpclient.cpp',
  'network/smbclient.cpp',
  'network/netvolumes.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'print', 'panels', 'network'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)

# The login window greetd shows (session/zacos9-greeter runs it).
executable('zacos9-login',
  'greeter/main.cpp',
  'greeter/greetd.cpp',
  'finder/power.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'print', 'panels', 'greeter'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
  install_dir: get_option('libexecdir') / 'zacos9',
)

# Talking to greetd, against a stand-in for its socket.
test('greetd-client',
  executable('test-greetd',
    'greeter/tests/test_greetd.cpp',
    'greeter/greetd.cpp',
    include_directories: include_directories('greeter'),
    dependencies: [qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)

# The Pack: Mac OS 9's Application palette, torn off the Application menu.
pack_exe = executable('zacos9-pack',
  'pack/main.cpp', 'pack/packwindow.cpp', 'pack/apps.cpp', 'pack/toplevels.cpp',
  'finder/platinumshell.cpp', finder_protocol, client_protocol_src,
  include_directories: include_directories('finder', 'print', 'pack'),
  dependencies: [platinum_dep, qt6, layershellqt, wayland_client, gio, gio_unix],
  install: true,
)
test('pack-apps',
  executable('test-pack-apps', 'pack/tests/test_apps.cpp', 'pack/apps.cpp',
    include_directories: include_directories('pack'),
    dependencies: [platinum_dep, qt6, gio, gio_unix],
  ),
)

# Scribble (SimpleText) and Dog Ears (Stickies).
scribble_exe = executable('zacos9-scribble',
  'scribble/main.cpp', 'scribble/scribblewindow.cpp', 'scribble/textfile.cpp',
  'finder/alert.cpp', 'finder/platinumshell.cpp', finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'print', 'panels', 'scribble'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)
test('scribble-speech',
  executable('test-scribble-speech',
    'scribble/tests/test_speech.cpp', 'scribble/scribblewindow.cpp', 'scribble/textfile.cpp',
    'finder/alert.cpp', 'finder/platinumshell.cpp', finder_protocol,
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'panels', 'scribble'),
    dependencies: [platinum_dep, qt6, wayland_client],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 30,
)
executable('zacos9-dogears',
  'dogears/main.cpp', 'dogears/notes.cpp', 'scribble/textfile.cpp',
  'finder/alert.cpp', 'finder/platinumshell.cpp', finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'print', 'panels', 'scribble', 'dogears'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)
executable('zacos9-notepad',
  'notepad/main.cpp', 'notepad/notepadwindow.cpp',
  'finder/platinumshell.cpp', finder_protocol,
  include_directories: include_directories('finder', 'notepad'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)
executable('zacos9-stash',
  'stash/main.cpp', 'stash/stash.cpp', 'stash/stashwindow.cpp',
  'finder/platinumshell.cpp', finder_protocol,
  include_directories: include_directories('finder', 'stash'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)
executable('zacos9-barkrecorder',
  'barkrecorder/main.cpp', 'barkrecorder/barkrecorder.cpp',
  'finder/platinumshell.cpp', finder_protocol,
  include_directories: include_directories('finder', 'barkrecorder'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)
test('scribble-files',
  executable('test-scribble-files', 'scribble/tests/test_textfile.cpp', 'scribble/textfile.cpp',
    include_directories: include_directories('scribble'),
    dependencies: [qt6],
  ),
)
test('notepad',
  executable('test-notepad', 'notepad/test_notepad.cpp', 'notepad/notepadwindow.cpp',
    include_directories: include_directories('notepad'),
    dependencies: [qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
)
test('stash',
  executable('test-stash', 'stash/test_stash.cpp', 'stash/stash.cpp', 'stash/stashwindow.cpp',
    include_directories: include_directories('stash'),
    dependencies: [qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
)
test('bark-recorder',
  executable('test-bark-recorder', 'barkrecorder/test_barkrecorder.cpp',
    'barkrecorder/barkrecorder.cpp',
    include_directories: include_directories('barkrecorder'),
    dependencies: [qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
)
test('dog-ears-notes',
  executable('test-dog-ears-notes', 'dogears/tests/test_notes.cpp', 'dogears/notes.cpp',
    include_directories: include_directories('dogears'),
    dependencies: [qt6],
  ),
)

# Printing over CUPS: Printer Picker (the Chooser) and print queues.
print_sources = ['print/printers.cpp', 'print/desktopprinter.cpp', 'finder/alert.cpp',
  'finder/platinumshell.cpp', finder_protocol]
executable('zacos9-printerpicker', 'print/picker_main.cpp', 'print/pickerwindow.cpp', print_sources,
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels', 'print'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)
executable('zacos9-printqueue', 'print/queue_main.cpp', 'print/queuewindow.cpp', print_sources,
  link_with: panelkit,
  include_directories: include_directories('finder', 'panels', 'print'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)
test('printing',
  executable('test-printing', 'print/tests/test_printing.cpp', 'print/pickerwindow.cpp',
    'print/queuewindow.cpp', print_sources,
    link_with: panelkit,
    include_directories: include_directories('finder', 'panels', 'print'),
    dependencies: [platinum_dep, qt6, wayland_client],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)

# The Help Center (Mac Help).
help_sources = ['help/helpwindow.cpp', 'help/helpbook.cpp', 'finder/platinumshell.cpp', finder_protocol]
executable('zacos9-help', 'help/main.cpp', help_sources,
  link_with: panelkit,
  include_directories: include_directories('finder', 'print', 'panels', 'help'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)
test('help-center',
  executable('test-help-center', 'help/tests/test_help.cpp', help_sources,
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'panels', 'help'),
    dependencies: [platinum_dep, qt6, wayland_client],
  ),
  env: ['QT_QPA_PLATFORM=offscreen', 'ZACOS9_HELP_SOURCE=' + meson.project_source_root() / 'share/zacos9/help'],
  timeout: 30,
)

# Pedigree (Apple System Profiler).
pedigree_sources = ['pedigree/pedigreewindow.cpp', 'pedigree/profile.cpp',
  'finder/alert.cpp', 'finder/platinumshell.cpp', finder_protocol]
executable('zacos9-pedigree', 'pedigree/main.cpp', pedigree_sources,
  link_with: panelkit,
  include_directories: include_directories('finder', 'print', 'panels', 'pedigree'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)
test('pedigree',
  executable('test-pedigree', 'pedigree/tests/test_pedigree.cpp', pedigree_sources,
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'panels', 'pedigree'),
    dependencies: [platinum_dep, qt6, wayland_client],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 30,
)

# The Calculator desk accessory.
executable('zacos9-calculator',
  'calculator/main.cpp', 'calculator/engine.cpp',
  include_directories: include_directories('finder', 'print', 'calculator'),
  dependencies: [platinum_dep, qt6],
  install: true,
)
test('calculator',
  executable('test-calculator', 'calculator/tests/test_engine.cpp', 'calculator/engine.cpp',
    include_directories: include_directories('calculator'),
    dependencies: [qt6],
  ),
)
test('internet-defaults',
  executable('test-internet-defaults', 'panels/tests/test_defaultapps.cpp', 'panels/defaultapps.cpp',
    include_directories: include_directories('panels'),
    dependencies: [qt6, gio, gio_unix],
  ),
)

executable('zacos9-picture',
  'picture/main.cpp',
  'picture/picturewindow.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'print', 'panels', 'picture'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)
picture_test = executable('test-picture', 'picture/tests/test_picture.cpp',
  'picture/picturewindow.cpp', 'finder/alert.cpp', 'finder/platinumshell.cpp', finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'print', 'panels', 'picture'),
  dependencies: [platinum_dep, qt6, wayland_client],
)
test('picture-viewer', picture_test, env: ['QT_QPA_PLATFORM=offscreen'], timeout: 30)

executable('zacos9-store',
  'store/main.cpp',
  'store/store.cpp',
  'store/storeclient.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  'finder/appdb.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'print', 'panels'),
  dependencies: [platinum_dep, qt6, wayland_client, gio, gio_unix],
  install: true,
)

test('store-browser',
  executable('test-store-browser',
    '../tests/store/test_browser.cpp',
    'store/store.cpp', 'store/storeclient.cpp',
    'finder/alert.cpp', 'finder/platinumshell.cpp', 'finder/appdb.cpp',
    finder_protocol,
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'panels', 'store'),
    dependencies: [platinum_dep, qt6, wayland_client, gio, gio_unix],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)

# The Software window's catalog: reading it, filtering, and the helper
# script's own package-name validation, run directly (never through
# pkexec, and never with a package name that could really install or
# remove anything).
# Software Update's checks: reading a GitHub release and apt's simulation,
# and Debian version order (dpkg's own); no network, nothing installed.
test('update-check',
  executable('test-update-check',
    'update/tests/test_updatecheck.cpp',
    'update/updatecheck.cpp',
    include_directories: include_directories('update'),
    dependencies: [qt6],
  ),
  timeout: 60,
)

test('store-client',
  executable('test-store-client',
    'store/tests/test_storeclient.cpp',
    'store/storeclient.cpp',
    include_directories: include_directories('finder', 'print', 'store'), # settings.h, storeclient.h
    dependencies: [platinum_dep, qt6],
  ),
  timeout: 60,
)

# The Keyboard panel's layouts, read as zacos9-wm reads them.
xkb_layouts_sources = ['panels/xkblayouts.cpp', '../compositor/src/kblayout.c']
# The Internet panel's defaults, through GIO.
default_apps_sources = ['panels/defaultapps.cpp', 'panels/functionkeys.cpp']
xkb_layouts_include = include_directories('../compositor/src')
# Key Tags (Key Caps), drawn from the same keymap as zacos9-wm's.
executable('zacos9-keytags',
  'keytags/main.cpp', 'keytags/keytags.cpp', 'keytags/keytagswindow.cpp', xkb_layouts_sources,
  'finder/platinumshell.cpp', finder_protocol,
  include_directories: [include_directories('finder', 'keytags', 'panels'), xkb_layouts_include],
  dependencies: [platinum_dep, qt6, wayland_client, xkbcommon_client],
  install: true,
)
test('key-tags',
  executable('test-key-tags', 'keytags/test_keytags.cpp', 'keytags/keytags.cpp',
    'keytags/keytagswindow.cpp', xkb_layouts_sources,
    include_directories: [include_directories('keytags', 'panels'), xkb_layouts_include],
    dependencies: [platinum_dep, qt6, xkbcommon_client],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
)

startup_disk_sources = ['panels/startupdisk.cpp', 'finder/alert.cpp', 'finder/platinumshell.cpp', finder_protocol]
memory_sources = ['panels/memory.cpp']
# Memory with a fake helper (memory.cpp).
test('memory-panel',
  executable('test-memory-panel', '../tests/memory/test_panel.cpp', memory_sources,
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'panels'),
    dependencies: [platinum_dep, qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 30,
)
# Startup Disk with a fake helper (startupdisk.cpp).
test('startupdisk-panel',
  executable('test-startupdisk-panel', '../tests/startupdisk/test_panel.cpp', startup_disk_sources,
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'panels'),
    dependencies: [platinum_dep, qt6, wayland_client],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 30,
)
crypt = dependency('libxcrypt')
users_sources = ['panels/users.cpp']
# Users with a stand-in accountsservice on a private session bus (users.cpp).
test('users-panel', find_program('dbus-run-session'),
  args: ['--', executable('test-users-panel', '../tests/users/test_panel.cpp', users_sources,
      'finder/alert.cpp', 'finder/platinumshell.cpp', finder_protocol,
      link_with: panelkit,
      include_directories: include_directories('finder', 'panels'),
      dependencies: [platinum_dep, qt6, wayland_client, crypt, gio_unix],
    ).full_path(), files('../tests/users/fake_accounts.py')],
  timeout: 60,
)
controlpanel_exe = executable('zacos9-controlpanel',
  'panels/controlpanel.cpp',
  'panels/soundclient.cpp',
  'finder/alert.cpp',
  users_sources,
  xkb_layouts_sources, default_apps_sources, startup_disk_sources, memory_sources,
  link_with: panelkit,
  include_directories: [include_directories('finder'), xkb_layouts_include],
  dependencies: [platinum_dep, qt6, xkbcommon_client, gio, gio_unix, wayland_client, crypt],
  install: true,
)

collar_sources = [
  'collar/collar.cpp', 'collar/collarart.cpp', 'collar/systemcontrols.cpp',
  'collar/displaycontrols.cpp', 'collar/kdeconnect.cpp', 'collar/keychain.cpp', 'collar/media.cpp',
  'collar/filesharing.cpp', 'collar/printing.cpp', 'collar/extensions.cpp', 'print/printers.cpp', 'panels/sharingclient.cpp', 'panels/soundclient.cpp', 'finder/alert.cpp', 'finder/platinumshell.cpp',
  finder_protocol,
]
collar_exe = executable('zacos9-collar', 'collar/main.cpp', collar_sources,
  link_with: panelkit,
  include_directories: include_directories('collar', 'finder', 'panels', 'print'),
  dependencies: [platinum_dep, qt6, layershellqt, wayland_client],
  install: true,
)

collar_ui_test = executable('test-collar', 'collar/tests/test_collar.cpp', collar_sources,
  link_with: panelkit,
  include_directories: include_directories('collar', 'finder', 'panels', 'print'),
  dependencies: [platinum_dep, qt6, layershellqt, wayland_client],
)
# No media players, File Sharing settings or printers from the computer running the tests.
collar_test_env = ['ZACOS9_MPRIS_PREFIX=org.zacos9.test.NoMpris.', 'ZACOS9_SHARING_CONF=/nonexistent',
  'ZACOS9_CUPS_BIN=' + meson.current_source_dir() / 'collar/tests/no-printers']
test('collar-ui', collar_ui_test,
  env: ['QT_QPA_PLATFORM=offscreen', 'ZACOS9_KDECONNECT_SERVICE=org.zacos9.test.NoKdeConnect',
    'ZACOS9_SECRETS_SERVICE=org.zacos9.test.NoSecrets',
    'ZACOS9_COLLAR_EXTENSION_PREFIX=org.zacos9.test.NoExtensions'] + collar_test_env, timeout: 30)
collar_kdeconnect_test = executable('test-collar-kdeconnect', 'collar/tests/test_kdeconnect.cpp',
  collar_sources,
  link_with: panelkit,
  include_directories: include_directories('collar', 'finder', 'panels', 'print'),
  dependencies: [platinum_dep, qt6, layershellqt, wayland_client],
)
test('collar-kdeconnect', find_program('dbus-run-session'),
  args: ['--', collar_kdeconnect_test],
  env: ['QT_QPA_PLATFORM=offscreen', 'ZACOS9_SECRETS_SERVICE=org.zacos9.test.NoSecrets'] + collar_test_env,
  timeout: 60,
)
collar_keychain_test = executable('test-collar-keychain', 'collar/tests/test_keychain.cpp',
  collar_sources,
  link_with: panelkit,
  include_directories: include_directories('collar', 'finder', 'panels', 'print'),
  dependencies: [platinum_dep, qt6, layershellqt, wayland_client],
)
test('collar-keychain', find_program('dbus-run-session'),
  args: ['--', collar_keychain_test], env: ['QT_QPA_PLATFORM=offscreen'] + collar_test_env, timeout: 60,
)
collar_media_test = executable('test-collar-media', 'collar/tests/test_media_sharing.cpp',
  collar_sources,
  link_with: panelkit,
  include_directories: include_directories('collar', 'finder', 'panels', 'print'),
  dependencies: [platinum_dep, qt6, layershellqt, wayland_client],
)
test('collar-media-sharing', find_program('dbus-run-session'),
  args: ['--', collar_media_test], env: ['QT_QPA_PLATFORM=offscreen'], timeout: 60,
)
collar_extensions_test = executable('test-collar-extensions', 'collar/tests/test_extensions.cpp',
  collar_sources,
  link_with: panelkit,
  include_directories: include_directories('collar', 'finder', 'panels', 'print'),
  dependencies: [platinum_dep, qt6, layershellqt, wayland_client],
)
test('collar-extensions', find_program('dbus-run-session'),
  args: ['--', collar_extensions_test], env: ['QT_QPA_PLATFORM=offscreen'] + collar_test_env, timeout: 60,
)
collar_controls_test = executable('test-collar-controls',
  'collar/tests/test_controls.cpp', 'collar/systemcontrols.cpp',
  include_directories: include_directories('collar'),
  dependencies: [qt6],
)
test('collar-controls', find_program('dbus-run-session'),
  args: ['--', collar_controls_test], timeout: 30,
)
collar_displays_test = executable('test-collar-displays',
  'collar/tests/test_displays.cpp', 'collar/displaycontrols.cpp',
  include_directories: include_directories('collar'),
  dependencies: [platinum_dep, qt6],
)
test('collar-displays', find_program('dbus-run-session'),
  args: ['--', collar_displays_test, files('collar/tests/fake-ddcutil.sh')], timeout: 30,
)

test('panel-hints',
  executable('test-panel-hints', 'panels/tests/test_hints.cpp',
    link_with: panelkit,
    include_directories: include_directories('finder', 'panels'),
    dependencies: [platinum_dep, qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'], timeout: 30,
)

test('collar-panel',
  executable('test-collar-panel',
    'panels/tests/test_collarpanel.cpp',
    'panels/collarpanel.cpp',
    'collar/collarart.cpp',
    'finder/platinumshell.cpp',
    finder_protocol,
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'collar', 'panels'),
    dependencies: [platinum_dep, qt6, wayland_client],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'], timeout: 30,
)

test('keyboard-layout-list',
  executable('test-xkblayouts',
    'panels/tests/test_xkblayouts.cpp', xkb_layouts_sources,
    include_directories: [include_directories('panels'), xkb_layouts_include],
    dependencies: [platinum_dep, qt6, xkbcommon_client],
  ),
)

test('sound-output',
  executable('test-sound-output',
    'panels/tests/test_sound.cpp',
    'panels/soundclient.cpp',
    include_directories: include_directories('panels'),
    dependencies: qt6,
  ),
  args: files('panels/tests/pactl-fixture.py'),
  timeout: 60,
)

test('system-power',
  executable('test-system-power', '../tests/session/test_power.cpp', 'finder/power.cpp',
    include_directories: include_directories('finder'),
    dependencies: [qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
)
installer_exe = executable('zacos9-installer',
  'installer/main.cpp',
  'installer/installer.cpp',
  'finder/power.cpp',
  # Disk Setup's plan and partition map, for "next to what's on it".
  'disksetup/plan.cpp', 'disksetup/udisks.cpp', 'disksetup/diskmap.cpp',
  link_with: panelkit,
  include_directories: include_directories('finder', 'print', 'panels', 'disksetup'),
  dependencies: [platinum_dep, qt6],
  install: true,
)

executable('zacos9-appinstall',
  'appinstall/main.cpp',
  'appinstall/appinstall.cpp',
  'finder/alert.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'print', 'panels'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)

# Software Update (Apple menu): ZacOS 9 releases from GitHub, Debian's
# updates through apt; installs through zacos9-appstore-helper.
# Software Update's daily check (a user timer; Check daily turns it off).
install_data('update/zacos9-update-check.service', 'update/zacos9-update-check.timer',
  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'user')

executable('zacos9-update',
  'update/main.cpp',
  'update/update.cpp',
  'update/updatecheck.cpp',
  'store/storeclient.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'print', 'panels', 'store'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)

executable('zacos9-wininstall',
  'wininstall/main.cpp',
  'wininstall/wininstall.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  link_with: panelkit,
  include_directories: include_directories('finder', 'print', 'panels'),
  dependencies: [platinum_dep, qt6, wayland_client],
  install: true,
)

executable('zacos9-setup',
  'setup/main.cpp',
  'setup/setup.cpp',
  link_with: panelkit,
  include_directories: include_directories('finder', 'print', 'panels'),
  dependencies: [platinum_dep, qt6],
  install: true,
)

# vptr: scripted pointer (press/drag/release) for UI tests.
fullscreen_client = executable('test-fullscreen-client',
  '../compositor/tests/fullscreen_client.cpp',
  dependencies: qt6,
)
test('fullscreen-protocols', find_program('python3'),
  args: [files('../compositor/tests/test_fullscreen.py'),
    meson.project_build_root() / 'compositor/zacos9-wm', fullscreen_client],
  depends: compositor_exe,
  timeout: 30,
)
test('fullscreen-protocols-hidpi', find_program('python3'),
  args: [files('../compositor/tests/test_fullscreen.py'),
    meson.project_build_root() / 'compositor/zacos9-wm', fullscreen_client, '2'],
  depends: compositor_exe,
  timeout: 30,
)

vptr_protocol = []
foreach kind : [['client-header', '@BASENAME@-client-protocol.h'], ['private-code', '@BASENAME@-protocol.c']]
  vptr_protocol += custom_target('vptr_' + kind[0].underscorify(),
    input: meson.project_source_root() / 'protocols/wlr-virtual-pointer-unstable-v1.xml',
    output: kind[1],
    command: [wayland_scanner, kind[0], '@INPUT@', '@OUTPUT@'],
  )
endforeach
vptr_exe = executable('vptr', meson.project_source_root() / 'tools/vptr/vptr.c', vptr_protocol,
  client_protocol_src,
  dependencies: [wayland_client, xkbcommon_client])
test('disksetup-partition-map', find_program('python3'),
  args: [disksetup_map_test, compositor_exe.full_path(), disksetup_exe, vptr_exe],
  depends: compositor_exe,
  timeout: 90)
test('installer-beside', find_program('python3'),
  args: [files('../tests/installer/test_beside.py'), compositor_exe.full_path(), installer_exe, vptr_exe],
  depends: compositor_exe,
  timeout: 90)

# One screen's exact pixels for UI tests (grim resamples at 1.5x and 1.75x).
rawshot_exe = executable('rawshot', meson.project_source_root() / 'tools/rawshot/rawshot.c',
  snapshot_protocol,
  dependencies: [wayland_client])

# Screen saver, display and system sleep (compositor/src/idle.c).
idle_client = executable('test-idle-client',
  '../compositor/tests/idle_client.cpp',
  dependencies: qt6,
)
# The same, saying where its focus is as ZacOS's own programs do.
field_client = executable('test-field-client',
  '../compositor/tests/idle_client.cpp',
  'finder/platinumshell.cpp',
  finder_protocol,
  include_directories: include_directories('finder'),
  dependencies: [qt6, wayland_client],
  cpp_args: ['-DPLATINUM_FIELD'],
)
# The on-screen keyboard for a text field with no keyboard attached.
test('onscreen-keyboard', find_program('python3'),
  args: [files('../compositor/tests/test_onscreen_keyboard.py'), compositor_exe, idle_client, field_client,
    vptr_exe, rawshot_exe],
  depends: [keyboard_exe],
  timeout: 120,
)
test('idle-sleep', find_program('python3'),
  args: [files('../compositor/tests/test_idle.py'), compositor_exe, idle_client, vptr_exe],
  timeout: 60,
)
test('idle-lock', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(), files('../compositor/tests/test_lock.py'),
    compositor_exe, idle_client, test_lock_exe, inhibit_exe, vptr_exe],
  timeout: 90,
)
# The lock at 1.5x and 1.75x: the screen's own pixels, only its 1x colours.
test('lock-scale', find_program('python3'),
  args: [files('../compositor/tests/test_lock_scale.py'), compositor_exe, test_lock_exe, vptr_exe,
    rawshot_exe],
  timeout: 90,
)
# ⌘-Tab, Option-click on the desktop, contextual menus and Log Out, with the
# real menu bar and Sniffer.
test('session-ui', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(), files('../compositor/tests/test_session_ui.py'),
    compositor_exe, menubar_exe, finder_exe, idle_client, vptr_exe],
  timeout: 90,
)
# The Pack: torn off the Application menu (menubar/main.c, pack/).
test('pack-session', find_program('python3'),
  args: [files('../compositor/tests/test_pack.py'), compositor_exe, menubar_exe, idle_client, vptr_exe],
  depends: [pack_exe],
  timeout: 60,
)
# Function keys that open things (fkeys.c, input.c).
test('function-keys-session', find_program('python3'),
  args: [files('../compositor/tests/test_function_keys.py'), compositor_exe, idle_client, vptr_exe],
  timeout: 60,
)
# Sticky Keys, Mouse Keys and Zoom (access.c, input.c, output.c).
test('accessibility-session', find_program('python3'),
  args: [files('../compositor/tests/test_access.py'), compositor_exe, idle_client, vptr_exe],
  timeout: 90,
)
# Proxy icons and spring-open collapsed windows (decor.c, input.c, folderwindow.cpp).
test('proxy-icon', find_program('python3'),
  args: [files('../compositor/tests/test_proxy_icon.py'), compositor_exe, menubar_exe, finder_exe, vptr_exe],
  timeout: 90,
)
# Pop-up windows: tabs at the bottom of the screen (decor.c, view.c, input.c, folderwindow.cpp).
test('popup-windows', find_program('python3'),
  args: [files('../compositor/tests/test_popup_windows.py'), compositor_exe, menubar_exe, finder_exe, vptr_exe],
  timeout: 120,
)
# Mouse Tracks: a trail behind the moving pointer (compositor/src/tracks.c).
test('mouse-tracks', find_program('python3'),
  args: [files('../compositor/tests/test_mouse_tracks.py'), compositor_exe, vptr_exe,
    scribble_exe],
  timeout: 60,
)
# Spring-loaded folders: Space during a drag opens one at once (drag_spring).
test('spring-space', find_program('python3'),
  args: [files('../compositor/tests/test_spring_space.py'), compositor_exe, menubar_exe, finder_exe, vptr_exe],
  timeout: 120,
)
# The Qt programs at 1.25x, 1.5x and 1.75x: a panel exactly its 1x picture,
# each pixel whole screen pixels.
test('panel-scale', find_program('python3'),
  args: [files('../compositor/tests/test_panel_scale.py'), compositor_exe, controlpanel_exe,
    rawshot_exe],
  timeout: 120,
)
# Screen Sharing's viewer against a real wayvnc, the guest at 1.5x.
test('screenshare-viewer', find_program('python3'),
  args: [files('../tests/screenshare/test_viewer.py'), compositor_exe, screenshare_viewer, idle_client,
    vptr_exe, rawshot_exe],
  timeout: 120,
)
# Window frames at 1.25x, 1.5x and 1.75x: the screen's own pixels, even stripes.
test('frame-scale', find_program('python3'),
  args: [files('../compositor/tests/test_frame_scale.py'), compositor_exe, idle_client, vptr_exe,
    rawshot_exe],
  timeout: 120,
)
# The menu bar at 1.25x, 1.5x and 1.75x: the 1x one, each pixel whole screen pixels.
test('menubar-scale', find_program('python3'),
  args: [files('../compositor/tests/test_menubar_scale.py'), compositor_exe, menubar_exe, vptr_exe,
    rawshot_exe],
  timeout: 120,
)
# The menu bar on every screen, each at its own scale; menus open where clicked.
test('menubar-displays', find_program('python3'),
  args: [files('../compositor/tests/test_menubar_displays.py'), compositor_exe, menubar_exe, vptr_exe],
  timeout: 90,
)
# Monitors' 1.25x, 1.5x and 1.75x, per screen: wp_fractional_scale_v1, but not for ZacOS's own programs.
globals_client = executable('test-globals-client',
  '../compositor/tests/globals_client.c',
  dependencies: [wayland_client],
)
test('fractional-scale', find_program('python3'),
  args: [files('../compositor/tests/test_fractional_scale.py'), compositor_exe, globals_client],
  timeout: 60,
)
# Keyboard layouts: ⌘-Space and the keyboard menu (input.c, kbmenu.c).
test('keyboard-layout-switch', find_program('python3'),
  args: [files('../compositor/tests/test_keyboard_layouts.py'), compositor_exe, menubar_exe, vptr_exe],
  timeout: 60,
)
# Option-click on the close and collapse boxes, ⌘-drag behind (input.c).
close_app_client = executable('test-close-app-client',
  '../compositor/tests/close_app_client.c', finder_protocol,
  dependencies: [wayland_client],
)
test('window-tricks', find_program('python3'),
  args: [files('../compositor/tests/test_window_tricks.py'), compositor_exe, idle_client,
    vptr_exe, close_app_client],
  timeout: 60,
)
# A keyring password prompt keeps the keyboard until answered (compositor/src/view.c).
test('system-prompt', find_program('python3'),
  args: [files('../compositor/tests/test_system_prompt.py'), compositor_exe, idle_client,
    vptr_exe],
  timeout: 60,
)
test('idle-inhibit', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(), files('../compositor/tests/test_inhibit.py'),
    compositor_exe, idle_client, inhibit_exe],
  timeout: 60,
)

test('collar-native', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(), files('collar/tests/test_native.py'),
    compositor_exe, collar_exe, capture_exe, vptr_exe],
  timeout: 90,
)

busy_client = executable('test-busy-client',
  '../compositor/tests/busy_client.cpp', 'finder/platinumshell.cpp', finder_protocol,
  include_directories: include_directories('finder'),
  dependencies: [qt6, wayland_client],
)
test('busy-cursor-protocols', find_program('python3'),
  args: [files('../compositor/tests/test_busy_cursor.py'), compositor_exe, busy_client, vptr_exe,
    meson.project_source_root() / 'tools/cursors/build_cursors.py',
    meson.project_source_root() / 'assets/cursors/platinum-cursors.pcur'],
  depends: [compositor_exe, cursor_theme],
  timeout: 45,
)
test('ghost-frames', find_program('python3'),
  args: [files('../compositor/tests/test_ghost_frames.py'), compositor_exe, busy_client],
  depends: [compositor_exe],
  timeout: 60,
)

# ZacOS 9 Qt style plugin: other Qt applications (Qt 6) drawn with the
# Platinum controls. Selected with QT_STYLE_OVERRIDE=zacos9 (session/).
qt6_mod = import('qt6')
qtstyle_moc = qt6_mod.preprocess(moc_headers: 'qtstyle/zacos9style.h',
  dependencies: qt6)
shared_module('zacos9style',
  'qtstyle/zacos9style.cpp', qtstyle_moc,
  include_directories: include_directories('qtstyle'),
  dependencies: [platinum_dep, qt6],
  name_prefix: '',
  install: true,
  install_dir: get_option('libdir') / 'qt6' / 'plugins' / 'styles',
)
test('qtstyle-tooltips',
  executable('test-qtstyle-tooltips', 'qtstyle/test_tooltips.cpp',
    'qtstyle/zacos9style.cpp', qtstyle_moc,
    include_directories: include_directories('qtstyle'),
    dependencies: [platinum_dep, qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
)
gtk_menu_observer = executable('gtk-menu-observer',
  '../tests/finder/gtk_menu_observer.c', finder_protocol, 'menubar/appmenu.c',
  include_directories: include_directories('menubar'),
  dependencies: [platinum_dep, wayland_client, gio],
)
test('gtk-appmenu', find_program('dbus-run-session'),
  args: [executable('test-appmenu-gtk', 'menubar/test_appmenu_gtk.c',
    dependencies: [platinum_dep, wayland_client, gio])],
  env: ['G_DEBUG=fatal-warnings'], timeout: 15)
gtk3 = dependency('gtk+-3.0')
gtk_parser = dependency('appmenu-gtk3-parser')
gtk_appmenu_module = shared_module('zacos9-appmenu',
  'gtkmenu/module.c',
  dependencies: [gtk3, gtk_parser],
  install: true,
  install_dir: get_option('libdir') / 'gtk-3.0' / 'modules',
)
test('gtk-save-places', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(),
    files('../tests/finder/test_desktop_gtk.py'), compositor_exe.full_path(),
    files('../session/zacos9-desktop-places'), gtk_appmenu_module.full_path()],
  depends: [gtk_appmenu_module, compositor_exe],
  timeout: 30,
)
test('gtk-theme-css',
  executable('test-gtk-theme', '../tests/finder/test_gtk_theme.c',
    dependencies: [gtk3]),
  args: [meson.project_source_root() / 'share/themes/ZacOS9/gtk-3.0/gtk.css'],
)
qt_menu_fixture = executable('qt-menu-fixture', '../tests/finder/qt_menu_fixture.cpp',
  dependencies: qt6)
test('gtk-menu-protocol', find_program('dbus-run-session'),
  args: ['--', find_program('python3').full_path(),
    files('../tests/finder/test_gtk_menu_protocol.py'),
    compositor_exe.full_path(), gtk_menu_observer.full_path(),
    files('../tests/finder/gtk_menu_fixture.py'), gtk_appmenu_module.full_path(),
    qt_menu_fixture.full_path()],
  depends: [compositor_exe, gtk_menu_observer, gtk_appmenu_module, qt_menu_fixture],
  timeout: 45,
)
test('finder-desktop',
  executable('test-finder-desktop',
    '../tests/finder/test_desktop.cpp', finder_test_sources,
    'finder/filechooserportal.cpp',
    'qtstyle/zacos9style.cpp', qtstyle_moc,
    link_with: panelkit,
    include_directories: include_directories('finder', 'print', 'panels', 'network', 'qtstyle'),
    dependencies: [platinum_dep, qt6, layershellqt, wayland_client, gio, gio_unix, finder_fonts_dep],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
  timeout: 60,
)

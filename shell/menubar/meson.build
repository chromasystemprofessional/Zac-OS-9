wayland_cursor = dependency('wayland-cursor')

test('erase-disk-menu',
  executable('test-erase-menu', '../../tests/menubar/test_erase_menu.c',
    dependencies: [platinum_dep, wayland_client],
    c_args: ['-ffunction-sections', '-fdata-sections'],
    link_args: ['-Wl,--gc-sections']))

menubar_exe = executable('zacos9-menubar',
  'main.c',
  'menus.c',
  'toplevels.c',
  'launch.c',
  'keys.c',
  'finderlink.c',
  'appmenu.c',
  'logout.c',
  'recent.c',
  'kbmenu.c',
  'appicon.c',
  'hints.c',
  client_protocol_src,
  finder_protocol,
  dependencies: [platinum_dep, wayland_client, wayland_cursor, xkbcommon_client, gio,
    dependency('gdk-pixbuf-2.0'), appfilter_dep, controlpanels_dep],
  install: true,
)

# Hint Bubbles over the menu bar's menus (menubar/hints.c).
hints_test = executable('test-hints', '../../tests/menubar/test_hints.c', 'hints.c',
  dependencies: [platinum_dep, wayland_client],
)
test('menu-hints', hints_test, timeout: 10)
# The same, with the Italian catalog: labels and hints translated together.
test('menu-hints-it', find_program('python3'),
  args: [files('../../tests/menubar/test_hints_it.py'),
    meson.project_source_root(), hints_test],
  timeout: 60,
)
# And in Russian.
test('menu-hints-ru', find_program('python3'),
  args: [files('../../tests/menubar/test_hints_ru.py'),
    meson.project_source_root(), hints_test],
  timeout: 60,
)
# And in Spanish.
test('menu-hints-es', find_program('python3'),
  args: [files('../../tests/menubar/test_hints_es.py'),
    meson.project_source_root(), hints_test],
  timeout: 60,
)
# And in Brazilian Portuguese.
test('menu-hints-pt', find_program('python3'),
  args: [files('../../tests/menubar/test_hints_pt.py'),
    meson.project_source_root(), hints_test],
  timeout: 60,
)

# The Zac menu's recent items (menubar/recent.c).
test('menu-recent',
  executable('test-recent', '../../tests/menubar/test_recent.c', 'recent.c',
    dependencies: [platinum_dep, gio],
  ),
  timeout: 10,
)

# The keyboard menu (menubar/kbmenu.c).
test('menu-keyboard',
  executable('test-kbmenu', '../../tests/menubar/test_kbmenu.c', 'kbmenu.c',
    dependencies: [platinum_dep, wayland_client],
  ),
  timeout: 10,
)

# ⌘-Tab's choice of program (menubar/toplevels.c).
test('menu-app-switch',
  executable('test-app-switch',
    '../../tests/menubar/test_app_switch.c', 'toplevels.c', client_protocol_src,
    dependencies: [platinum_dep, wayland_client, gio],
  ),
  timeout: 10,
)

test('menu-launch-feedback',
  executable('test-menu-launch',
    '../../tests/menubar/test_launch.c', 'launch.c',
    dependencies: [platinum_dep, wayland_client, gio, appfilter_dep, controlpanels_dep],
  ),
  timeout: 10,
)
gtk_menu_observer = executable('gtk-menu-observer',
  '../../tests/finder/gtk_menu_observer.c', finder_protocol, 'appmenu.c',
  dependencies: [platinum_dep, wayland_client, gio],
)
test('gtk-appmenu', find_program('dbus-run-session'),
  args: [executable('test-appmenu-gtk', '../../tests/menubar/test_appmenu_gtk.c',
    dependencies: [platinum_dep, wayland_client, gio])],
  env: ['G_DEBUG=fatal-warnings'], timeout: 15)

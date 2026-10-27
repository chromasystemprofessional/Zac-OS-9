wayland_cursor = dependency('wayland-cursor')

test('erase-disk-menu',
  executable('test-erase-menu', 'test_erase_menu.c',
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
test('menu-hints',
  executable('test-hints', 'test_hints.c', 'hints.c',
    dependencies: [platinum_dep, wayland_client],
  ),
  timeout: 10,
)

# The Zac menu's recent items (menubar/recent.c).
test('menu-recent',
  executable('test-recent', 'test_recent.c', 'recent.c',
    dependencies: [platinum_dep, gio],
  ),
  timeout: 10,
)

# The keyboard menu (menubar/kbmenu.c).
test('menu-keyboard',
  executable('test-kbmenu', 'test_kbmenu.c', 'kbmenu.c',
    dependencies: [platinum_dep, wayland_client],
  ),
  timeout: 10,
)

# ⌘-Tab's choice of program (menubar/toplevels.c).
test('menu-app-switch',
  executable('test-app-switch',
    'test_app_switch.c', 'toplevels.c', client_protocol_src,
    dependencies: [platinum_dep, wayland_client],
  ),
  timeout: 10,
)

test('menu-launch-feedback',
  executable('test-menu-launch',
    'test_launch.c', 'launch.c',
    dependencies: [platinum_dep, wayland_client, gio, appfilter_dep, controlpanels_dep],
  ),
  timeout: 10,
)
gtk_menu_observer = executable('gtk-menu-observer',
  '../../tests/finder/gtk_menu_observer.c', finder_protocol, 'appmenu.c',
  dependencies: [platinum_dep, wayland_client, gio],
)
test('gtk-appmenu', find_program('dbus-run-session'),
  args: [executable('test-appmenu-gtk', 'test_appmenu_gtk.c',
    dependencies: [platinum_dep, wayland_client, gio])],
  env: ['G_DEBUG=fatal-warnings'], timeout: 15)

# Pure drawing code shared by the compositor, the shell and the tests.
cairo = dependency('cairo')
threads = dependency('threads')

platinum_inc = include_directories('.')
platinum_lib = static_library('platinum',
  'draw.c',
  'text.c',
  'menudraw.c',
  'widgets.c',
  'icons.c',
  'logo.c',
  'welcome.c',
  'extensions.c',
  'patterns.c',
  'savers.c',
  'settings.c',
  'electron.c',
  # Shared-memory buffers for the C Wayland programs. Only their headers
  # here: the programs link libwayland-client themselves, and the
  # compositor, which never uses these, doesn't.
  'wlclient.c',
  include_directories: platinum_inc,
  dependencies: [cairo, threads,
    dependency('wayland-client').partial_dependency(compile_args: true, includes: true)],
  c_args: [
    '-DZACOS9_DATA_DIR="@0@"'.format(get_option('prefix') / get_option('datadir') / 'zacos9'),
    '-DZACOS9_SOURCE_DIR="@0@"'.format(meson.project_source_root()),
  ],
)

install_data(
  files(
    '../assets/sounds/platinum.wav', '../assets/sounds/glass.wav',
    '../assets/sounds/droplet.wav', '../assets/sounds/pluck.wav',
    '../assets/sounds/woodblock.wav', '../assets/sounds/chirp.wav',
  ),
  install_dir: get_option('datadir') / 'zacos9' / 'sounds',
)
# The Software window's catalog (shell/store).
install_data('../assets/store/catalog.json',
  install_dir: get_option('datadir') / 'zacos9' / 'store',
)
platinum_dep = declare_dependency(
  link_with: platinum_lib,
  include_directories: platinum_inc,
  dependencies: [cairo, threads],
)

# Pixel tests against the Apple HIG figures (tools/measure/fetch.sh).
figs_dir = meson.project_source_root() / 'tools/measure/figs'
# Limited accounts' program filter (malcontent, over accountsservice).
appfilter_lib = static_library('appfilter', 'appfilter.c',
  dependencies: dependency('gio-unix-2.0'))
appfilter_dep = declare_dependency(link_with: appfilter_lib,
  include_directories: include_directories('.'),
  dependencies: dependency('gio-unix-2.0'))
test('appfilter', executable('test-appfilter', 'tests/test_appfilter.c',
  dependencies: [appfilter_dep]))

figcompare_dep = declare_dependency(
  sources: 'tests/figcompare.c',
  include_directories: include_directories('tests'),
  dependencies: [platinum_dep],
)
test('menudraw',
  executable('test-menudraw', 'tests/test_menudraw.c',
    dependencies: [figcompare_dep]),
  args: [figs_dir],
)
test('text', executable('test-text', 'tests/test_text.c', dependencies: [platinum_dep]))
test('draw', executable('test-draw', 'tests/test_draw.c', dependencies: [platinum_dep]))
test('welcome', executable('test-welcome', 'tests/test_welcome.c', dependencies: [platinum_dep]))
test('settings', executable('test-settings', 'tests/test_settings.c', dependencies: [platinum_dep]))
test('savers', executable('test-savers', 'tests/test_savers.c', dependencies: [platinum_dep]))
test('electron', executable('test-electron', 'tests/test_electron.c', dependencies: [platinum_dep]))
test('widgets',
  executable('test-widgets', 'tests/test_widgets.c',
    dependencies: [figcompare_dep]),
  args: [figs_dir],
)

# The compiled font table must match the .pfont sources.
python3 = find_program('python3', required: false)
if python3.found()
  sound_driver = executable('test-sound-driver', 'tests/sound_driver.c',
    dependencies: [platinum_dep])
  test('sound-playback', python3,
    args: [files('tests/test_sound.py'), sound_driver,
      meson.project_source_root() / 'assets'],
    timeout: 60)
  test('fonts-current', python3,
    args: [meson.project_source_root() / 'tools/fonts/build_fonts.py', '--check'])
  # The TrueType copies too (skipped without python3-fonttools).
  test('fonts-ttf-current', python3,
    args: [meson.project_source_root() / 'tools/fonts/build_ttf.py', '--check'])
endif
if python3.found()
  test('icons-current', python3,
    args: [meson.project_source_root() / 'tools/icons/build_icons.py', '--check'])
endif
if python3.found()
  test('patterns-current', python3,
    args: [meson.project_source_root() / 'tools/patterns/make_patterns.py', '--check'])
endif

# Pure drawing code shared by the compositor, the shell and the tests.
cairo = dependency('cairo')

platinum_inc = include_directories('.')
platinum_lib = static_library('platinum',
  'draw.c',
  'text.c',
  'menudraw.c',
  'widgets.c',
  'icons.c',
  'logo.c',
  'welcome.c',
  'patterns.c',
  'settings.c',
  include_directories: platinum_inc,
  dependencies: [cairo],
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
  dependencies: [cairo],
)

# Pixel tests against the Apple HIG figures (tools/measure/fetch.sh).
figs_dir = meson.project_source_root() / 'tools/measure/figs'
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
test('widgets',
  executable('test-widgets', 'tests/test_widgets.c',
    dependencies: [figcompare_dep]),
  args: [figs_dir],
)

# The compiled font table must match the .pfont sources.
python3 = find_program('python3', required: false)
if python3.found()
  test('fonts-current', python3,
    args: [meson.project_source_root() / 'tools/fonts/build_fonts.py', '--check'])
endif
if python3.found()
  test('icons-current', python3,
    args: [meson.project_source_root() / 'tools/icons/build_icons.py', '--check'])
endif
if python3.found()
  test('patterns-current', python3,
    args: [meson.project_source_root() / 'tools/patterns/make_patterns.py', '--check'])
endif

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
  include_directories: platinum_inc,
  dependencies: [cairo],
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
test('widgets',
  executable('test-widgets', 'tests/test_widgets.c',
    dependencies: [figcompare_dep]),
  args: [figs_dir],
)

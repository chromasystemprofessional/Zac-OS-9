# ZacOS 9 Qt style plugin: other Qt applications (Qt 6) drawn with the
# Platinum controls. Selected with QT_STYLE_OVERRIDE=zacos9 (session/).
qt6_mod = import('qt6')
qtstyle_moc = qt6_mod.preprocess(moc_headers: 'zacos9style.h',
  dependencies: qt6)
shared_module('zacos9style',
  'zacos9style.cpp', qtstyle_moc,
  dependencies: [platinum_dep, qt6],
  name_prefix: '',
  install: true,
  install_dir: get_option('libdir') / 'qt6' / 'plugins' / 'styles',
)
test('qtstyle-tooltips',
  executable('test-qtstyle-tooltips', '../../tests/qtstyle/test_tooltips.cpp',
    'zacos9style.cpp', qtstyle_moc,
    dependencies: [platinum_dep, qt6],
  ),
  env: ['QT_QPA_PLATFORM=offscreen'],
)

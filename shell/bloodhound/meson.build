# Bloodhound: Sherlock 2's Files channel in one window. The search engine
# is libzacos9-files (findcriteria.cpp, findindex.cpp); the window and the
# worker are here.
qt6_mod = import('qt6')
bloodhound_moc = qt6_mod.preprocess(moc_headers: 'search.h', dependencies: qt6)

bloodhound_exe = executable('zacos9-bloodhound',
  'main.cpp',
  'bloodhoundwindow.cpp',
  'search.cpp', bloodhound_moc,
  include_directories: include_directories('../panels'),
  dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6],
  install: true,
)

test('bloodhound-search', executable('test-bloodhound-search',
    '../../tests/bloodhound/test_search.cpp', 'search.cpp', bloodhound_moc,
    include_directories: include_directories('../panels'),
    dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6],
  build_by_default: false,
),
  timeout: 30,
)

test('bloodhound-controls', executable('test-bloodhound-controls',
    '../../tests/bloodhound/test_controls.cpp', 'bloodhoundwindow.cpp', 'search.cpp',
    bloodhound_moc,
    include_directories: include_directories('../panels'),
    dependencies: [shell_files_dep, shell_common_dep, platinum_dep, qt6],
  build_by_default: false,
),
  timeout: 30,
)

test('bloodhound-window', find_program('python3'),
  args: [files('../../tests/bloodhound/test_window.py'), compositor_exe,
    bloodhound_exe, vptr_exe],
  timeout: 60,
)

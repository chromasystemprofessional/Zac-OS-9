# Retriever: folder pairs kept alike with rsync (retriever/sync.cpp).
executable('zacos9-retriever',
  'main.cpp', 'sync.cpp',
  link_with: panelkit,
  include_directories: include_directories('../finder', '../panels'),
  dependencies: [shell_common_dep, platinum_dep, qt6, wayland_client],
  install: true,
)
test('retriever-sync',
  executable('test-retriever-sync', 'test_sync.cpp', 'sync.cpp',
    dependencies: [qt6],
  ),
  timeout: 30,
)

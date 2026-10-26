# zacos9-finder-ctl: scripting the Sniffer over its socket.
finderctl_exe = executable('zacos9-finder-ctl', 'main.c',
  dependencies: [platinum_dep],
  install: true,
)
test('finder-ctl', find_program('python3'),
  args: [files('test_finderctl.py'), finderctl_exe.full_path()],
  depends: finderctl_exe, timeout: 20)

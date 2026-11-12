# The screen saver (docs/energy-saver.md).
#
# The After Dark helper is a Rust binary built from the vendored MIT engine
# (emulation/afterdark). Cargo is optional: a build without it (or with
# -Dafterdark=disabled) still has the built-in savers, and the panel says the
# helper is missing. -Dafterdark=enabled makes a missing cargo an error.
afterdark_lib = static_library('afterdark', 'afterdark.c',
  dependencies: [platinum_dep],
)

build_afterdark = false
if get_option('afterdark') != 'disabled'
  cargo = find_program('cargo', required: false)
  if not cargo.found()
    # rustup installs outside PATH unless the person adds ~/.cargo/bin;
    # use it rather than making a plain ninja fail on a reconfigure.
    home = run_command('sh', '-c', 'printf %s "$HOME"', check: false).stdout().strip()
    if home != ''
      cargo = find_program(home / '.cargo/bin/cargo',
        required: get_option('afterdark') == 'enabled')
    endif
  endif
  build_afterdark = cargo.found()
endif
if build_afterdark
  afterdark_dir = meson.project_source_root() / 'emulation/afterdark'
  afterdark_build = find_program(afterdark_dir / 'build.sh')
  afterdark_sources = run_command('find', afterdark_dir,
    '-path', afterdark_dir / 'target', '-prune', '-o', '-type', 'f', '-print',
    check: true).stdout().strip().split('\n')
  custom_target('zacos9-afterdark',
    output: 'zacos9-afterdark',
    command: [afterdark_build, '@OUTPUT@', meson.current_build_dir() / 'afterdark-target', cargo],
    depend_files: afterdark_sources,
    build_by_default: true,
    install: true,
    install_dir: get_option('bindir'),
  )
endif

saver_exe = executable('zacos9-screensaver',
  'screensaver.c',
  client_protocol_src,
  link_with: afterdark_lib,
  dependencies: [platinum_dep, wayland_client],
  install: true,
)

test('afterdark-frame',
  executable('test-afterdark', '../../tests/screensaver/test_afterdark.c',
    link_with: afterdark_lib,
    include_directories: include_directories('.'),
    dependencies: [platinum_dep],
    build_by_default: false,
  ),
  timeout: 10,
)

project('zacos9', 'c',
  version: '1.18.35',
  license: 'GPL-3.0-or-later',
  meson_version: '>=1.0.0',
  default_options: ['c_std=c11', 'cpp_std=c++17', 'warning_level=2', 'werror=false'],
)

add_project_arguments(
  '-DWLR_USE_UNSTABLE',
  '-D_POSIX_C_SOURCE=200809L',
  '-Wno-unused-parameter',
  language: 'c',
)

# Tests must never draw on the desktop they are built from: a test that opened
# windows on the live compositor once crashed it and logged the user out. Tests
# that need a display start their own headless zacos9-wm and override these.
add_test_setup('isolated',
  env: {'WAYLAND_DISPLAY': 'zacos9-test-no-display', 'DISPLAY': '',
    # No live desktop state: with a theme installed, window text renders
    # in the theme's faces, so metric assertions need the built-ins.
    'XDG_CONFIG_HOME': meson.project_build_root() / 'test-home',
    # Tests run from the build tree: find the checkout's assets even when a
    # package build compiled in a fixed installed path instead.
    'ZACOS9_SOURCE': meson.project_source_root(),
    # No host disks: otherwise a developer's USB drive makes the Finder
    # offer to erase it over the test window.
    'ZACOS9_DISK_HELPER': '',
    # The Python tests' shared helpers (tests/harness/zacos9test.py).
    'PYTHONPATH': meson.project_source_root() / 'tests/harness'},
  is_default: true,
)

subdir('lib')
subdir('compositor')
subdir('shell')
subdir('share')
subdir('session')
subdir('boot')
subdir('hardware')
subdir('system')
subdir('network')
subdir('po')

# Everything the package installs, against tests/baseline/installed-files.txt
# (the restructuring's safety net: a file that drops out of the package fails).
test('installed-files', find_program('python3'),
  args: [files('tests/baseline/test_installed_files.py'), meson.project_build_root(),
    files('tests/baseline/installed-files.txt')])

# The signed package repository: layout, signatures, APT update and download,
# and keeping the previous release's package.
test('repo', find_program('python3'),
  args: [files('tests/repo/test_repo.py'), files('scripts/build-repo.sh')],
  timeout: 180)

# The three binary packages partition what the build installs.
test('package-split', find_program('python3'),
  args: [files('tests/baseline/test_package_split.py'),
    meson.project_source_root()])

# Static units (no [Install]) are on demand, not started by the package
# scripts, and the login window's compositor has seatd for its seat.
test('packaging', find_program('python3'),
  args: [files('tests/baseline/test_packaging.py'), meson.project_source_root()])

# ZacOS 9 does not track its users: no known tracker endpoint in shipped
# source, and the update, Software and web-search code only contact the hosts
# PRIVACY.md lists. A new network feature must be added to both.
test('privacy', find_program('python3'),
  args: [files('tests/privacy/test_no_tracking.py'), meson.project_source_root()])

# The privileged helpers under system/ are shell scripts. ShellCheck's
# warnings are errors here; the Python helpers are out of scope. The Ubuntu
# package build installs shellcheck from debian/control's Build-Depends.
test('system-shellcheck', find_program('python3'),
  args: [files('tests/system/test_shellcheck.py'), meson.project_source_root()])

# One version everywhere: the changelog, Meson, PROJECT_STATUS, the README's
# "New in" section and a What's New note for every release since 1.18.19.
test('release-truth', find_program('python3'),
  args: [files('tests/release/test_release_truth.py'),
    meson.project_version(), meson.project_source_root()])

# The release SBOM: CycloneDX, packages plus the emulators' GPL source,
# and deterministic output for a given SOURCE_DATE_EPOCH.
test('sbom', find_program('python3'),
  args: [files('tests/release/test_sbom.py'), meson.project_source_root()])

# The Mac OS 9 theme extractor (theming step 3).
test('theme-extractor', find_program('python3'),
  args: [files('tests/tools/test_extract_macos9.py'),
    meson.project_source_root() / 'tools' / 'extract-macos9-theme.py'])

# The HFS/HFS+ reader behind the Theme Installer's System Folder copy.
test('mac-hfs', find_program('python3'),
  args: [files('tests/tools/test_mac_hfs.py'),
    meson.project_source_root() / 'tools' / 'mac-hfs.py'])

# The extractor the Theme Installer runs, and its decoder modules.
install_data(
  'tools/extract-macos9-theme.py',
  'tools/mac-hfs.py',
  'tools/macicons.py',
  'tools/cursors/build_cursors.py',
  'tools/macfont.py',
  'tools/macpict.py',
  'tools/macsnd.py',
  'tools/macpat.py',
  'tools/macfont.py',
  'tools/pngwrite.py',
  install_dir: get_option('datadir') / 'zacos9' / 'tools')

# Mac OS 9 icon decoder (theming, phase A).
test('macicons', find_program('python3'),
  args: [files('tests/tools/test_macicons.py'),
    meson.project_source_root() / 'tools' / 'macicons.py'])

# Mac OS 9 PICT decoder (theming, phase A).
test('macpict', find_program('python3'),
  args: [files('tests/tools/test_macpict.py'),
    meson.project_source_root() / 'tools' / 'macpict.py'])

# Mac OS 9 sound decoder (theming, phase A).
test('macsnd', find_program('python3'),
  args: [files('tests/tools/test_macsnd.py'),
    meson.project_source_root() / 'tools' / 'macsnd.py'])

# Mac OS 9 bitmap font reader (theming, fonts).
test('macfont', find_program('python3'),
  args: [files('tests/tools/test_macfont.py'),
    meson.project_source_root() / 'tools' / 'macfont.py'])

# Mac OS 9 pattern decoder (theming, phase A).
test('macpat', find_program('python3'),
  args: [files('tests/tools/test_macpat.py'),
    meson.project_source_root() / 'tools' / 'macpat.py'])

# Mac OS 9 font decoder (theming, phase D).
test('macfont', find_program('python3'),
  args: [files('tests/tools/test_macfont.py'),
    meson.project_source_root() / 'tools' / 'macfont.py'])

# PNG writer and scaling used by the theme extractor.
test('pngwrite', find_program('python3'),
  args: [files('tests/tools/test_pngwrite.py'),
    meson.project_source_root() / 'tools' / 'pngwrite.py'])

subdir('applications')
subdir('themes')
subdir('xdg')
subdir('polkit')
# The lock screen's own PAM service (zacos9-lock falls back to "login").
install_data('pam.d/zacos9-lock', install_dir: get_option('sysconfdir') / 'pam.d')
# The Help Center's books (zacos9-help).
install_subdir('zacos9/help', install_dir: get_option('datadir') / 'zacos9')
# ZacOS 9's own fonts as TrueType, for programs and the Fonts folder; built
# from the .pfont sources by tools/fonts/build_ttf.py (test fonts-ttf-current
# in lib/ checks they're current).
install_data('../assets/fonts/ZacOS9System-Regular.ttf', '../assets/fonts/ZacOS9Views-Regular.ttf',
  '../assets/fonts/ZacOS9Views-Italic.ttf',
  install_dir: get_option('datadir') / 'fonts' / 'truetype' / 'zacos9')
# The fonts programs see in a ZacOS session (zacos9-session sets FONTCONFIG_FILE).
install_data('fontconfig/zacos9-fonts.conf', install_dir: get_option('datadir') / 'zacos9' / 'fontconfig')
test('fonts-for-programs', find_program('python3'),
  args: [files('../tests/fonts/test_fonts_for_programs.py'), files('fontconfig/zacos9-fonts.conf'),
    files('../shell/finder/fonts.cpp')],
  timeout: 180)

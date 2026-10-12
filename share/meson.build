subdir('applications')
subdir('themes')
subdir('xdg')
subdir('polkit')
# The lock screen's own PAM service (zacos9-lock falls back to "login").
install_data('pam.d/zacos9-lock', install_dir: get_option('sysconfdir') / 'pam.d')

# Desktop entries: copied into build/share/applications (platinum-wm adds
# its ../share to XDG_DATA_DIRS, which covers the build tree and an
# install alike) and installed.
entries = ['platinum-appearance.desktop', 'platinum-datetime.desktop']
foreach f : entries
  configure_file(input: f, output: f, copy: true)
endforeach
install_data(entries, install_dir: get_option('datadir') / 'applications')

# Desktop entries: copied into build/share/applications (zacos9-wm adds
# its ../share to XDG_DATA_DIRS, which covers the build tree and an
# install alike) and installed.
entries = ['zacos9-appearance.desktop', 'zacos9-datetime.desktop', 'zacos9-filesharing.desktop',
  'zacos9-keyboard.desktop', 'zacos9-monitors.desktop', 'zacos9-mouse.desktop',
  'zacos9-sound.desktop', 'zacos9-tcpip.desktop', 'zacos9-store.desktop',
  'zacos9-netbrowser.desktop', 'zacos9-setup.desktop', 'zacos9-installer.desktop',
  'zacos9-wininstall.desktop', 'zacos9-appinstall.desktop']
foreach f : entries
  configure_file(input: f, output: f, copy: true)
endforeach
install_data(entries, install_dir: get_option('datadir') / 'applications')

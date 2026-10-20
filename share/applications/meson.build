# Desktop entries: copied into build/share/applications (zacos9-wm adds
# its ../share to XDG_DATA_DIRS, which covers the build tree and an
# install alike) and installed.
entries = ['zacos9-appearance.desktop', 'zacos9-collarpanel.desktop', 'zacos9-datetime.desktop',
  'zacos9-energysaver.desktop', 'zacos9-fileexchange.desktop', 'zacos9-filesharing.desktop',
  'zacos9-keyboard.desktop', 'zacos9-monitors.desktop', 'zacos9-mouse.desktop', 'zacos9-screensaver.desktop',
  'zacos9-sound.desktop', 'zacos9-tablet.desktop', 'zacos9-tcpip.desktop', 'zacos9-zacmenu.desktop', 'zacos9-general.desktop', 'zacos9-internet.desktop', 'zacos9-trackpad.desktop', 'zacos9-accessibility.desktop', 'zacos9-startupdisk.desktop', 'zacos9-users.desktop', 'zacos9-calculator.desktop', 'zacos9-pedigree.desktop', 'zacos9-help.desktop', 'zacos9-printerpicker.desktop', 'zacos9-printqueue.desktop', 'zacos9-dogears.desktop', 'zacos9-scribble.desktop', 'zacos9-notepad.desktop', 'zacos9-stash.desktop', 'zacos9-keytags.desktop', 'zacos9-barkrecorder.desktop', 'zacos9-store.desktop',
  'zacos9-netbrowser.desktop', 'zacos9-setup.desktop', 'zacos9-installer.desktop',
  'zacos9-wininstall.desktop', 'zacos9-appinstall.desktop', 'zacos9-update.desktop',
  'zacos9-bluetooth.desktop', 'zacos9-picture.desktop', 'zacos9-keychain.desktop', 'zacos9-retriever.desktop', 'zacos9-speech.desktop', 'zacos9-memory.desktop', 'zacos9-numbers.desktop', 'zacos9-text.desktop', 'zacos9-disksetup.desktop', 'zacos9-walkies.desktop', 'zacos9-plotter.desktop', 'zacos9-jigsaw.desktop',
  'zacos9-finder.desktop']
foreach f : entries
  configure_file(input: f, output: f, copy: true)
endforeach
install_data(entries, install_dir: get_option('datadir') / 'applications')

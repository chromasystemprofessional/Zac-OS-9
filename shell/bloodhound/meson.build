# Bloodhound: the search window. For now it asks the Sniffer for its Find
# window; the program becomes a search window of its own in a later change.
bloodhound = configure_file(input: 'zacos9-bloodhound', output: 'zacos9-bloodhound', copy: true)
install_data('zacos9-bloodhound', install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')

test('bloodhound-launch', find_program('python3'),
  args: [files('../../tests/bloodhound/test_launch.py'), bloodhound],
  timeout: 20)

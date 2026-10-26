gtk_parser = dependency('appmenu-gtk3-parser')
gtk_appmenu_module = shared_module('zacos9-appmenu',
  'module.c',
  dependencies: [gtk3, gtk_parser],
  install: true,
  install_dir: get_option('libdir') / 'gtk-3.0' / 'modules',
)

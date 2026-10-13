ui_sources = files('main.js', 'window.js', 'mpris.js', 'style.css')
pkgdatadir = get_option('prefix') / get_option('datadir') / meson.project_name()
libdir = get_option('prefix') / get_option('libdir')
install_data(ui_sources, install_dir: pkgdatadir)

# Installed launcher.
installed_conf = configuration_data()
installed_conf.set('MAIN_JS', pkgdatadir / 'main.js')
installed_conf.set('TYPELIB_DIR', libdir / 'girepository-1.0')
installed_conf.set('LIB_DIR', libdir)
configure_file(
  input: 'woodshed.in',
  output: 'woodshed-installed',
  configuration: installed_conf)
install_data(meson.current_build_dir() / 'woodshed-installed',
  rename: 'woodshed',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')

# Dev launcher: JS from the source tree, library from the build tree. No rebuild for UI edits.
dev_conf = configuration_data()
dev_conf.set('MAIN_JS', meson.current_source_dir() / 'main.js')
dev_conf.set('TYPELIB_DIR', meson.project_build_root() / 'gobject')
dev_conf.set('LIB_DIR', meson.project_build_root() / 'gobject')
launcher = configure_file(
  input: 'woodshed.in',
  output: 'woodshed',
  configuration: dev_conf)

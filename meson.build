project('woodshed', 'c', 'cpp',
  version: '0.1',
  license: 'GPL-2.0-or-later',
  license_files: ['COPYING'],
  meson_version: '>= 1.4',
  default_options: ['cpp_std=c++20', 'c_std=c11', 'warning_level=2'])

# Designated initializers for C event structs.
add_project_arguments('-Wno-missing-field-initializers', language: ['c', 'cpp'])

gnome = import('gnome')

api_version = '0.1'
app_id = 'io.github.kebabroyal.Woodshed'
dev_app_id = app_id + '.Devel'

subdir('core')
subdir('gobject')
subdir('ui')
subdir('data')

devenv = environment()
devenv.prepend('GI_TYPELIB_PATH', meson.current_build_dir() / 'gobject')
devenv.prepend('LD_LIBRARY_PATH', meson.current_build_dir() / 'gobject')
devenv.prepend('PATH', meson.current_build_dir() / 'ui')
meson.add_devenv(devenv)

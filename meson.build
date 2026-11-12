project('woodshed', 'c', 'cpp',
  version: '0.1',
  license: 'GPL-3.0-or-later',
  license_files: ['COPYING'],
  meson_version: '>= 1.4',
  default_options: ['cpp_std=c++20', 'c_std=c11', 'warning_level=2'])

# Designated initializers for C event structs.
add_project_arguments('-Wno-missing-field-initializers', language: ['c', 'cpp'])

gnome = import('gnome')
i18n = import('i18n')

api_version = '0.1'
app_id = 'io.github.kebabroyal.Woodshed'
dev_app_id = app_id + '.Devel'

subdir('lib')
subdir('src')
subdir('data')
subdir('po')
if get_option('tests')
  subdir('tests')
endif

devenv = environment()
devenv.prepend('GI_TYPELIB_PATH', meson.current_build_dir() / 'lib')
devenv.prepend('LD_LIBRARY_PATH', meson.current_build_dir() / 'lib')
devenv.prepend('PATH', meson.current_build_dir() / 'src')
meson.add_devenv(devenv)

gnome.post_install(
  gtk_update_icon_cache: true,
  update_desktop_database: true)

launcher_conf = configuration_data()
launcher_conf.set('MAIN_JS', meson.current_source_dir() / 'main.js')

# Runs JS from the source tree: no rebuild for UI edits.
configure_file(
  input: 'woodshed.in',
  output: 'woodshed',
  configuration: launcher_conf)

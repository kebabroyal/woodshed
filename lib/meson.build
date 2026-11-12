subdir('core')

gtk_dep = dependency('gtk4')

public_headers = files(
  'woodshed.h',
  'woodshed-engine.h',
  'woodshed-source.h',
  'woodshed-waveform.h')

enums = gnome.mkenums_simple('woodshed-enums',
  sources: files('woodshed-engine.h', 'woodshed-source.h', 'woodshed-waveform.h'),
  identifier_prefix: 'Woodshed',
  symbol_prefix: 'woodshed')

woodshed_lib = shared_library('woodshed-' + api_version,
  'woodshed-engine.cpp',
  'woodshed-source.cpp',
  'woodshed-waveform.cpp',
  enums,
  dependencies: [gtk_dep, core_dep],
  gnu_symbol_visibility: 'default',
  install: true)

woodshed_gir = gnome.generate_gir(woodshed_lib,
  sources: [public_headers, enums],
  namespace: 'Woodshed',
  nsversion: api_version,
  identifier_prefix: 'Woodshed',
  symbol_prefix: 'woodshed',
  includes: ['Gio-2.0', 'Gtk-4.0'],
  header: 'woodshed.h',
  install: true)

icon = files('icons' / app_id + '.svg')
install_data(icon, install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')

data_conf = configuration_data()
data_conf.set('APP_ID', app_id)
data_conf.set('NAME', 'Woodshed')
data_conf.set('EXEC', 'woodshed')

desktop = configure_file(
  input: app_id + '.desktop.in',
  output: app_id + '.desktop',
  configuration: data_conf,
  install_dir: get_option('datadir') / 'applications')

metainfo = configure_file(
  input: app_id + '.metainfo.xml.in',
  output: app_id + '.metainfo.xml',
  configuration: data_conf,
  install_dir: get_option('datadir') / 'metainfo')

# Dev build: own app ID, runs the dev launcher. Not installed.
dev_conf = configuration_data()
dev_conf.set('APP_ID', dev_app_id)
dev_conf.set('NAME', 'Woodshed (dev)')
dev_conf.set('EXEC', launcher.full_path())
dev_desktop = configure_file(
  input: app_id + '.desktop.in',
  output: dev_app_id + '.desktop',
  configuration: dev_conf)
dev_icon = files('icons' / dev_app_id + '.svg')

desktop_validate = find_program('desktop-file-validate', required: false)
if desktop_validate.found()
  test('desktop file', desktop_validate, args: [desktop])
  test('dev desktop file', desktop_validate, args: [dev_desktop])
endif
appstreamcli = find_program('appstreamcli', required: false)
if appstreamcli.found()
  # No homepage until the project is published.
  test('metainfo', appstreamcli,
    args: ['validate', '--no-net', '--override=url-homepage-missing=info', metainfo])
endif

# meson compile -C build install-desktop
run_target('install-desktop',
  command: [files('install-desktop.sh'), dev_desktop, dev_icon])

icondir = get_option('datadir') / 'icons' / 'hicolor'
install_data('icons/hicolor/scalable/apps' / app_id + '.svg', install_dir: icondir / 'scalable' / 'apps')
install_data('icons/hicolor/symbolic/apps' / app_id + '-symbolic.svg', install_dir: icondir / 'symbolic' / 'apps')

data_conf = configuration_data()
data_conf.set('APP_ID', app_id)
data_conf.set('EXEC', 'woodshed')

desktop = i18n.merge_file(
  input: configure_file(
    input: app_id + '.desktop.in',
    output: app_id + '.desktop.in',
    configuration: data_conf),
  output: app_id + '.desktop',
  type: 'desktop',
  po_dir: '../po',
  install: true,
  install_dir: get_option('datadir') / 'applications')

metainfo = i18n.merge_file(
  input: configure_file(
    input: app_id + '.metainfo.xml.in',
    output: app_id + '.metainfo.xml.in',
    configuration: data_conf),
  output: app_id + '.metainfo.xml',
  po_dir: '../po',
  install: true,
  install_dir: get_option('datadir') / 'metainfo')

# Dev build: own app ID and name, runs the dev launcher. Not installed.
dev_conf = configuration_data()
dev_conf.set('APP_ID', dev_app_id)
dev_conf.set('EXEC', launcher.full_path())
dev_desktop = custom_target('dev-desktop',
  input: configure_file(
    input: app_id + '.desktop.in',
    output: dev_app_id + '.desktop.in',
    configuration: dev_conf),
  output: dev_app_id + '.desktop',
  command: ['sed', '-e', 's/^Name=Woodshed$/Name=Woodshed (dev)/', '@INPUT@'],
  capture: true,
  build_by_default: true)
dev_icon = files('icons/hicolor/scalable/apps' / dev_app_id + '.svg')

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

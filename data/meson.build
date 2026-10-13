icon = files('icons' / app_id + '.svg')
install_data(icon, install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')

data_conf = configuration_data()
data_conf.set('APP_ID', app_id)

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

desktop_validate = find_program('desktop-file-validate', required: false)
if desktop_validate.found()
  test('desktop file', desktop_validate, args: [desktop])
endif
appstreamcli = find_program('appstreamcli', required: false)
if appstreamcli.found()
  # No homepage until the project is published.
  test('metainfo', appstreamcli,
    args: ['validate', '--no-net', '--override=url-homepage-missing=info', metainfo])
endif

# meson compile -C build install-desktop
run_target('install-desktop',
  command: [files('install-desktop.sh'), desktop, icon, launcher.full_path()])

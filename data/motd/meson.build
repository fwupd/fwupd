
install_data(['fwupd-refresh.timer'],
  install_dir: systemdunitdir)

con2 = configuration_data()
con2.set('bindir', bindir)
con2.set('motd_file', join_paths(localstatedir, 'lib', 'fwupd', 'motd'))
configure_file(
  input : '85-fwupd.motd.in',
  output : '85-fwupd',
  configuration : con2,
  install: true,
  install_dir: join_paths(sysconfdir, 'update-motd.d')
)

configure_file(
  input : 'fwupd-refresh.service.in',
  output : 'fwupd-refresh.service',
  configuration : con2,
  install: true,
  install_dir: systemdunitdir)

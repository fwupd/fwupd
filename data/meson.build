subdir('builder')
subdir('pki')
subdir('remotes.d')
subdir('bash-completion')

if get_option('tests') and get_option('daemon')
  subdir('tests')
  subdir('installed-tests')
endif

install_data(['daemon.conf'],
  install_dir : join_paths(sysconfdir, 'fwupd')
)

install_data(['org.freedesktop.fwupd.metainfo.xml'],
  install_dir: join_paths(datadir, 'metainfo')
)

install_data(['org.freedesktop.fwupd.conf'],
  install_dir : join_paths(sysconfdir, 'dbus-1', 'system.d')
)

install_data(['metadata.xml'],
  install_dir : join_paths(datadir, 'fwupd', 'remotes.d', 'fwupd')
)

if get_option('daemon')
  install_data(['90-fwupd-devices.rules'],
    install_dir : join_paths(udevdir, 'rules.d')
  )
endif

con2 = configuration_data()
con2.set('libexecdir', libexecdir)
con2.set('bindir', bindir)
con2.set('localstatedir', localstatedir)
con2.set('datadir', datadir)
con2.set('bootdir', get_option('bootdir'))
con2.set('sysconfdir', default_sysconfdir)

# replace @libexecdir@
configure_file(
  input : 'org.freedesktop.fwupd.service.in',
  output : 'org.freedesktop.fwupd.service',
  configuration : con2,
  install: true,
  install_dir: join_paths(datadir,
                          'dbus-1',
                          'system-services'),
)

if get_option('systemd')
  # replace @bindir@
  configure_file(
    input : 'fwupd-offline-update.service.in',
    output : 'fwupd-offline-update.service',
    configuration : con2,
    install: true,
    install_dir: systemdunitdir,
  )
endif

if get_option('systemd')
  # replace @localstatedir@, @sysconfdir@ and @bootdir@
  configure_file(
    input : 'fwupd.service.in',
    output : 'fwupd.service',
    configuration : con2,
    install: true,
    install_dir: systemdunitdir,
  )
endif

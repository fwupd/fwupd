subdir('builder')
subdir('pki')
subdir('remotes.d')

if get_option('enable-tests')
  subdir('tests')
  subdir('installed-tests')
endif

install_data(['daemon.conf'],
  install_dir : join_paths(get_option('sysconfdir'), 'fwupd')
)

install_data(['org.freedesktop.fwupd.metainfo.xml'],
  install_dir: join_paths(get_option('datadir'), 'metainfo')
)

install_data(['org.freedesktop.fwupd.conf'],
  install_dir : join_paths(get_option('sysconfdir'), 'dbus-1', 'system.d')
)

install_data(['metadata.xml'],
  install_dir : join_paths(get_option('datadir'), 'fwupd', 'remotes.d', 'fwupd')
)

install_data(['90-fwupd-devices.rules'],
  install_dir : join_paths(udevdir, 'rules.d')
)

con2 = configuration_data()
con2.set('libexecdir',
         join_paths(get_option('prefix'),
                    get_option('libexecdir')))
con2.set('bindir',
         join_paths(get_option('prefix'),
                    get_option('bindir')))
con2.set('localstatedir', join_paths(get_option('localstatedir')))
con2.set('datadir', join_paths(get_option('datadir')))
con2.set('bootdir', get_option('with-bootdir'))
con2.set('sysconfdir', default_sysconfdir)

# replace @libexecdir@
configure_file(
  input : 'org.freedesktop.fwupd.service.in',
  output : 'org.freedesktop.fwupd.service',
  configuration : con2,
  install: true,
  install_dir: join_paths(get_option('datadir'),
                          'dbus-1',
                          'system-services'),
)

if get_option('enable-systemd')
  # replace @bindir@
  configure_file(
    input : 'fwupd-offline-update.service.in',
    output : 'fwupd-offline-update.service',
    configuration : con2,
    install: true,
    install_dir: systemdunitdir,
  )
endif

if get_option('enable-systemd')
  # replace @localstatedir@, @sysconfdir@ and @bootdir@
  configure_file(
    input : 'fwupd.service.in',
    output : 'fwupd.service',
    configuration : con2,
    install: true,
    install_dir: systemdunitdir,
  )
endif

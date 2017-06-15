subdir('tests')
subdir('installed-tests')
subdir('pki')

install_data(['fwupd.conf'],
  install_dir : get_option('sysconfdir')
)

install_data(['org.freedesktop.fwupd.metainfo.xml'],
  install_dir: join_paths(get_option('datadir'), 'metainfo')
)

install_subdir('remotes.d',
  install_dir: join_paths(get_option('sysconfdir'), 'fwupd')
)

install_data(['org.freedesktop.fwupd.conf'],
  install_dir : join_paths(get_option('sysconfdir'), 'dbus-1', 'system.d')
)

install_data(['org.freedesktop.fwupd.xml'],
  install_dir : join_paths(get_option('datadir'), 'app-info', 'xmls')
)

install_data(['90-fwupd-devices.rules'],
  install_dir : join_paths(udev.get_pkgconfig_variable('udevdir'), 'rules.d')
)

con2 = configuration_data()
con2.set('libexecdir',
         join_paths(get_option('prefix'),
                    get_option('libexecdir')))
con2.set('bindir',
         join_paths(get_option('prefix'),
                    get_option('bindir')))
con2.set('localstatedir', join_paths(get_option('localstatedir')))
con2.set('bootdir', get_option('with-bootdir'))

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
    install_dir: systemd.get_pkgconfig_variable('systemdsystemunitdir'),
  )
endif

if get_option('enable-systemd')
  # replace @localstatedir@ and @bootdir@
  configure_file(
    input : 'fwupd.service.in',
    output : 'fwupd.service',
    configuration : con2,
    install: true,
    install_dir: systemd.get_pkgconfig_variable('systemdsystemunitdir'),
  )
endif

subdir('builder')
subdir('pki')
subdir('remotes.d')
subdir('bash-completion')
subdir('fish-completion')

if build_daemon
subdir('motd')
endif

if get_option('tests')
  subdir('tests')
endif
if build_daemon
  subdir('installed-tests')
endif

if build_standalone
  install_data(['daemon.conf'],
    install_dir : join_paths(sysconfdir, 'fwupd')
  )
endif

install_data(['org.freedesktop.fwupd.metainfo.xml'],
  install_dir: join_paths(datadir, 'metainfo')
)

install_data(['org.freedesktop.fwupd.svg'],
  install_dir : join_paths(datadir, 'icons', 'hicolor', 'scalable', 'apps')
)

if build_daemon
  install_data(['org.freedesktop.fwupd.conf'],
    install_dir : join_paths(datadir, 'dbus-1', 'system.d')
  )
endif

if build_daemon and get_option('gudev')
  install_data(['90-fwupd-devices.rules'],
    install_dir : join_paths(udevdir, 'rules.d')
  )
endif

if get_option('systemd') and build_daemon
  con2 = configuration_data()
  con2.set('libexecdir', libexecdir)
  con2.set('bindir', bindir)
  con2.set('datadir', datadir)
  con2.set('localstatedir', localstatedir)
  con2.set('package_name', meson.project_name())
  rw_directories = []
  if get_option('plugin_uefi')
    rw_directories += ['-/boot/efi', '-/efi/EFI', '-/boot/EFI']
  endif

  dynamic_options = []
  if systemd.version().version_compare('>= 232')
    dynamic_options += 'ProtectControlGroups=yes'
    dynamic_options += 'ProtectKernelModules=yes'
  endif
  if systemd.version().version_compare('>= 231')
    dynamic_options += 'RestrictRealtime=yes'
#    dynamic_options += 'MemoryDenyWriteExecute=yes'
    dynamic_options += ['ReadWritePaths=' + ' '.join(rw_directories)]
  else
    dynamic_options += ['ReadWriteDirectories=' + ' '.join(rw_directories)]
  endif
  con2.set('dynamic_options', '\n'.join(dynamic_options))
  con2.set('motd_dir', motd_dir)

  # replace @bindir@
  configure_file(
    input : 'fwupd-offline-update.service.in',
    output : 'fwupd-offline-update.service',
    configuration : con2,
    install: true,
    install_dir: systemdunitdir,
  )

  # replace @dynamic_options@
  configure_file(
    input : 'fwupd.service.in',
    output : 'fwupd.service',
    configuration : con2,
    install: true,
    install_dir: systemdunitdir,
  )

  # for activation
  configure_file(
    input : 'fwupd.shutdown.in',
    output : 'fwupd.shutdown',
    configuration : con2,
    install: true,
    install_dir: systemd.get_pkgconfig_variable('systemdshutdowndir'),
  )
endif

if (get_option('systemd') or get_option('elogind')) and build_daemon
  con2 = configuration_data()
  con2.set('libexecdir', libexecdir)

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
endif

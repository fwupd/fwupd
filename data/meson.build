subdir('builder')
subdir('pki')
subdir('remotes.d')

if get_option('bash_completion')
  subdir('bash-completion')
endif

if get_option('fish_completion')
  subdir('fish-completion')
endif

if get_option('tests')
subdir('device-tests')
endif

if build_daemon
subdir('motd')
endif

if get_option('tests')
  if build_daemon
    subdir('installed-tests')
  endif
endif

if build_standalone
  install_data(['daemon.conf'],
    install_dir : join_paths(sysconfdir, 'fwupd')
  )
  install_data(['power.quirk', 'cfi.quirk'],
    install_dir: join_paths(datadir, 'fwupd', 'quirks.d'))
endif

if get_option('metainfo')
  install_data(['org.freedesktop.fwupd.metainfo.xml'],
    install_dir: join_paths(datadir, 'metainfo')
  )
  install_data(['org.freedesktop.fwupd.svg'],
    install_dir : join_paths(datadir, 'icons', 'hicolor', 'scalable', 'apps')
  )
endif

if build_daemon
  install_data(['org.freedesktop.fwupd.conf'],
    install_dir : join_paths(datadir, 'dbus-1', 'system.d')
  )

  if gudev.found()
    install_data(['90-fwupd-devices.rules'],
      install_dir : join_paths(udevdir, 'rules.d')
    )
  endif

  if libsystemd.found()
    con2 = configuration_data()
    con2.set('libexecdir', libexecdir)
    con2.set('bindir', bindir)
    con2.set('datadir', datadir)
    con2.set('localstatedir', localstatedir)
    rw_directories = []
    if not get_option('plugin_uefi_capsule').disabled()
      rw_directories += ['-/boot/efi', '-/efi/EFI', '-/boot/EFI', '-/boot/grub']
    endif

    dynamic_options = []
    if systemd.version().version_compare('>= 232')
      dynamic_options += 'ProtectControlGroups=yes'
      dynamic_options += 'ProtectKernelModules=yes'
    endif
    if systemd.version().version_compare('>= 231')
      dynamic_options += 'RestrictRealtime=yes'
#     dynamic_options += 'MemoryDenyWriteExecute=yes'
      dynamic_options += ['ReadWritePaths=' + ' '.join(rw_directories)]
    else
      dynamic_options += ['ReadWriteDirectories=' + ' '.join(rw_directories)]
    endif
    #pull configuration/cache/state from /etc and /var only if prefix is /usr
    if get_option('prefix') == '/usr'
      dynamic_options += 'ConfigurationDirectory=fwupd'
      dynamic_options += 'StateDirectory=fwupd'
      dynamic_options += 'CacheDirectory=fwupd'
    endif
    if not get_option('plugin_redfish').disabled()
      dynamic_options += 'RestrictAddressFamilies=AF_NETLINK AF_UNIX AF_INET AF_INET6'
    else
      dynamic_options += 'RestrictAddressFamilies=AF_NETLINK AF_UNIX'
    endif
    con2.set('dynamic_options', '\n'.join(dynamic_options))
    con2.set('motd_dir', motd_dir)

    # replace @bindir@
    if offline.allowed()
      configure_file(
        input : 'fwupd-offline-update.service.in',
        output : 'fwupd-offline-update.service',
        configuration : con2,
        install: true,
        install_dir: systemdunitdir,
      )
    endif

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
      install_dir: systemd_shutdown_dir,
    )
  endif

  if libsystemd.found() or elogind.found()
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
endif

subdir('bios-settings.d')
subdir('pki')
subdir('remotes.d')
subdir('icons')

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
    subdir('tests')
  endif
endif

if build_standalone
  install_data(
    ['fwupd.conf'],
    install_tag: 'runtime',
    install_dir: join_paths(sysconfdir, 'fwupd'),
    install_mode: 'rw-r-----',
  )
  plugin_quirks += files('cfi.quirk', 'ds20.quirk', 'power.quirk', 'vendors.quirk')
endif

if get_option('metainfo')
  custom_target(
    'metainfo',
    input: 'org.freedesktop.fwupd.metainfo.xml',
    output: 'org.freedesktop.fwupd.metainfo.xml',
    command: [generate_metainfo, '@INPUT@', '@OUTPUT@'],
    install: true,
    install_tag: 'doc',
    install_dir: join_paths(datadir, 'metainfo'),
  )
  install_data(
    ['org.freedesktop.fwupd.svg'],
    install_tag: 'doc',
    install_dir: join_paths(datadir, 'icons', 'hicolor', 'scalable', 'apps'),
  )
endif

if build_daemon
  install_data(
    ['org.freedesktop.fwupd.conf'],
    install_tag: 'runtime',
    install_dir: join_paths(datadir, 'dbus-1', 'system.d'),
  )

  build_conf = configuration_data()
  build_conf.set('libexecdir', libexecdir)

  if libsystemd.found()
    build_conf.set('bindir', bindir)
    build_conf.set('datadir', datadir)
    build_conf.set('localstatedir', localstatedir)
    build_conf.set('systemd_service', 'SystemdService=fwupd.service')
    rw_directories = []
    if allow_uefi
      rw_directories += [
        '-/boot/efi',
        '-/boot/EFI',
        '-/boot/grub',
        '-/efi/EFI',
        '-/sys/firmware/efi/efivars',
      ]
    endif

    # not using ConfigurationDirectory further down
    if get_option('prefix') != '/usr'
      rw_directories += join_paths(sysconfdir, 'fwupd')
    endif

    dynamic_options = []
    dynamic_options += ['ReadWritePaths=' + ' '.join(rw_directories)]
    #pull configuration/cache/state from /etc and /var only if prefix is /usr
    if get_option('prefix') == '/usr'
      dynamic_options += 'ConfigurationDirectory=fwupd'
      dynamic_options += 'StateDirectory=fwupd'
      dynamic_options += 'CacheDirectory=fwupd'
    endif

    device_allows = [
      'block-sd',
      'char-aux',
      'char-cpu/*',
      'char-drm',
      'char-gpiochip',
      'char-hidraw',
      'char-mei',
      'char-mtd',
      'char-tpm',
      'char-usb',
      'char-usb_device',
    ]
    if allow_flashrom
      device_allows += ['char-mem']
    endif
    if cc.has_header(
      'linux/nvme_ioctl.h',
      required: false,
    )
      device_allows += ['char-nvme']
    endif
    if allow_uefi
      # for BLKSSZGET
      device_allows += ['block-blkext']
    endif
    if get_option('plugin_modem_manager').allowed()
      device_allows += ['char-wwan_port']
    endif
    foreach device_allow : device_allows
      dynamic_options += ['DeviceAllow=' + device_allow + ' rw']
    endforeach

    # allow-list, see `systemd-analyze syscall-filter` for what each group adds
    syscall_filter = [
      '@basic-io',
      '@file-system',
      '@io-event',
      '@ipc',
      '@network-io',
      '@process',
      '@sync',
      '@signal', # for g_local_file_monitor_new
      '@timer', # for usbi_create_timer
      '@chown', # for sqlite3Step
      'ioctl',
      'uname',
      'fadvise64',
      'sysinfo', # for sysconf
      'madvise', # for mtrim
      'mremap', # for g_realloc
      'splice',
      'vmsplice',
      'copy_file_range', # for g_file_copy
    ]
    if allow_flashrom
      syscall_filter += ['@raw-io']
    endif
    dynamic_options += ['SystemCallFilter=' + ' '.join(syscall_filter)]
    if supported_build
      dynamic_options += ['Environment="FWUPD_SYSCALL_FILTER=systemd"']
      dynamic_options += ['SystemCallErrorNumber=EPERM']
    else
      # to debug what additional syscalls are required, we set SystemCallErrorNumber=SIGSYS
      # and then can do `coredumpctl debug` and get the syscall number with `(gdb) p $rax`
      dynamic_options += ['SystemCallErrorNumber=SIGSYS']
    endif
    if not supported_build
      dynamic_options += ['Environment="G_DEBUG=fatal-criticals"']
    endif
    build_conf.set('dynamic_options', '\n'.join(dynamic_options))
    build_conf.set('motd_dir', motd_dir)

    # replace @dynamic_options@
    configure_file(
      input: 'fwupd.service.in',
      output: 'fwupd.service',
      configuration: build_conf,
      install: true,
      install_tag: 'runtime',
      install_dir: systemdunitdir,
    )

    # for various plugins
    install_data(
      ['fwupd-i2c.conf'],
      install_tag: 'runtime',
      install_dir: systemd_modules_load_dir,
    )

    # for activation
    configure_file(
      input: 'fwupd.shutdown.in',
      output: 'fwupd.shutdown',
      configuration: build_conf,
      install: true,
      install_tag: 'runtime',
      install_dir: systemd_shutdown_dir,
    )
  else
    build_conf.set('systemd_service', '')
  endif

  configure_file(
    input: 'org.freedesktop.fwupd.service.in',
    output: 'org.freedesktop.fwupd.service',
    configuration: build_conf,
    install: true,
    install_tag: 'runtime',
    install_dir: join_paths(datadir, 'dbus-1', 'system-services'),
  )

  if launchctl.found()
    con2 = configuration_data()
    con2.set('libexecdir', libexecdir)
    con2.set('dbus_socket_address', get_option('dbus_socket_address'))
    configure_file(
      input: 'org.freedesktop.fwupd.plist',
      output: 'org.freedesktop.fwupd.plist',
      configuration: con2,
      install_tag: 'runtime',
      install_dir: get_option('launchd_agent_dir'),
    )
  endif

endif

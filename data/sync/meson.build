con2 = configuration_data()
con2.set('bindir', bindir)
con2.set('libdir', libdir)
con2.set('systemd_unit_user', get_option('systemd_unit_user'))

if libsystemd.found()
  if get_option('systemd_unit_user') == ''
      con2.set('user', 'DynamicUser=yes')
  else
      dynamic_options = [
                         'ProtectSystem=strict',
                         'ProtectHome=read-only',
                         'User=' + get_option('systemd_unit_user')
                        ]
      con2.set('user','\n'.join(dynamic_options))
  endif

  configure_file(
    input: 'fwupd-sync.service.in',
    output: 'fwupd-sync.service',
    configuration: con2,
    install: true,
    install_dir: systemdunitdir,
  )
endif

if libsystemd.found() and get_option('man')
  custom_target('fwupd-sync.service.8',
    input: 'fwupd-sync.service.md',
    output: 'fwupd-sync.service.8',
    command: [
      generate_man, '@INPUT@', '-o', '@OUTPUT@',
      '--replace', 'PACKAGE_VERSION', fwupd_version,
    ],
    install: true,
    install_dir: join_paths(mandir, 'man8'),
  )
endif

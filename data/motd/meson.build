
if libsystemd.found()
  install_data(['fwupd-refresh.timer'],
    install_dir: systemdunitdir)
  install_data(['fwupd-refresh.preset'],
    install_dir: systemdsystempresetdir)
  motd_fullpath = join_paths ('/run', motd_dir, motd_file)
else
  motd_fullpath = join_paths (localstatedir, motd_dir, motd_file)
endif

con2 = configuration_data()
con2.set('bindir', bindir)
con2.set('motd_fullpath', motd_fullpath)

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
    input: 'fwupd-refresh.service.in',
    output: 'fwupd-refresh.service',
    configuration: con2,
    install: true,
    install_dir: systemdunitdir,
  )
endif

# This file is only used in Ubuntu, which chooses to use update-motd instead
# of sourcing /run/motd.d/*
# See https://bugs.launchpad.net/ubuntu/+source/pam/+bug/399071
configure_file(
  input: '85-fwupd.motd.in',
  output: motd_file,
  configuration: con2,
  install: false,
)

if libsystemd.found() and get_option('man')
  custom_target('fwupd-refresh.service.8',
    input: 'fwupd-refresh.service.md',
    output: 'fwupd-refresh.service.8',
    command: [
      generate_man, '@INPUT@', '-o', '@OUTPUT@',
      '--replace', 'PACKAGE_VERSION', fwupd_version,
    ],
    install: true,
    install_dir: join_paths(mandir, 'man8'),
  )
endif

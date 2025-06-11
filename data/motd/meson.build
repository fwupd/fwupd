
if libsystemd.found()
  install_data(['fwupd-refresh.timer'],
    install_dir: systemdunitdir)
  motd_fullpath = join_paths ('/run', motd_dir, motd_file)
else
  motd_fullpath = join_paths (localstatedir, motd_dir, motd_file)
endif

if libsystemd.found()
  con2 = configuration_data()
  con2.set('bindir', bindir)
  con2.set('libdir', libdir)
  con2.set('motd_fullpath', motd_fullpath)

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

if libsystemd.found()
  if get_option('man')
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
  if build_docs
    md_targets += custom_target('fwupd-refresh.service.md',
      input: 'fwupd-refresh.service.md',
      output: 'fwupd-refresh.service.md',
      command: [
        generate_man, '@INPUT@', '-o', '@OUTPUT@',
        '--replace', 'PACKAGE_VERSION', fwupd_version,
        '--md',
      ],
    )
    man_md += ['"fwupd-refresh.service.md"']
  endif
endif

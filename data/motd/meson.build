motd_file = '85-fwupd'
motd_dir = 'motd.d'
fwupdmgr_path = join_paths (bindir, 'fwupdmgr')
motd_fullpath = join_paths ('/run', motd_dir, motd_file)
con2 = configuration_data()
con2.set('bindir', bindir)
con2.set('motd_file', motd_file)
con2.set('motd_dir', motd_dir)
con2.set('motd_fullpath', motd_fullpath)

if get_option('systemd')
  install_data(['fwupd-refresh.timer'],
    install_dir: systemdunitdir)

  dynamic_options = []
  if systemd.version().version_compare('>= 243')
    dynamic_options += 'ExecStart=' + fwupdmgr_path + ' get-updates --log ' + motd_file
  else
    dynamic_options += '# motd population disabled; requires systemd v243 or later'
  endif
  con2.set('dynamic_options', '\n'.join(dynamic_options))

  configure_file(
    input : 'fwupd-refresh.service.in',
    output : 'fwupd-refresh.service',
    configuration : con2,
    install: true,
    install_dir: systemdunitdir)
endif

# This file is only used in Ubuntu, which chooses to use update-motd instead
# of sourcing /run/motd.d/*
# See https://bugs.launchpad.net/ubuntu/+source/pam/+bug/399071
configure_file(
  input : '85-fwupd.motd.in',
  output : motd_file,
  configuration : con2,
  install: false,
)

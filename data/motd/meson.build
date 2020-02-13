
if get_option('systemd')
  install_data(['fwupd-refresh.timer'],
    install_dir: systemdunitdir)
  install_data(['fwupd-refresh.preset'],
    install_dir: systemdsystempresetdir)
  motd_fullpath = join_paths ('/run', motd_dir, 'fwupd', motd_file)
else
  motd_fullpath = join_paths (localstatedir, motd_dir, motd_file)
endif

con2 = configuration_data()
con2.set('bindir', bindir)
con2.set('motd_fullpath', motd_fullpath)

if get_option('systemd')
  configure_file(
    input : 'fwupd-refresh.service.in',
    output : 'fwupd-refresh.service',
    configuration : con2,
    install: true,
    install_dir: systemdunitdir,
  )
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

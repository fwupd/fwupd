if get_option('enable-lvfs')
  install_data([
      'lvfs.conf',
      'lvfs-testing.conf',
    ],
    install_dir : join_paths(get_option('sysconfdir'), 'fwupd', 'remotes.d')
  )
endif

install_data('README.md',
  install_dir : join_paths(get_option('datadir'), 'fwupd', 'remotes.d', 'vendor', 'firmware')
)

# replace @datadir@
con2 = configuration_data()
con2.set('datadir', join_paths(get_option('prefix'), get_option('datadir')))
configure_file(
  input : 'fwupd.conf',
  output : 'fwupd.conf',
  configuration : con2,
  install: true,
  install_dir: join_paths(get_option('sysconfdir'), 'fwupd', 'remotes.d'),
)
configure_file(
  input : 'vendor.conf',
  output : 'vendor.conf',
  configuration : con2,
  install: true,
  install_dir: join_paths(get_option('sysconfdir'), 'fwupd', 'remotes.d'),
)

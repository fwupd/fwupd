if build_daemon and get_option('lvfs')
  install_data([
      'lvfs.conf',
      'lvfs-testing.conf',
    ],
    install_dir : join_paths(sysconfdir, 'fwupd', 'remotes.d')
  )
  i18n.merge_file(
    input: 'lvfs.metainfo.xml',
    output: 'org.freedesktop.fwupd.remotes.lvfs.metainfo.xml',
    type: 'xml',
    po_dir: join_paths(meson.source_root(), 'po'),
    data_dirs: join_paths(meson.source_root(), 'po'),
    install: true,
    install_dir: join_paths(get_option('datadir'), 'fwupd', 'metainfo')
  )
  i18n.merge_file(
    input: 'lvfs-testing.metainfo.xml',
    output: 'org.freedesktop.fwupd.remotes.lvfs-testing.metainfo.xml',
    type: 'xml',
    po_dir: join_paths(meson.source_root(), 'po'),
    data_dirs: join_paths(meson.source_root(), 'po'),
    install: true,
    install_dir: join_paths(get_option('datadir'), 'fwupd', 'metainfo')
  )
endif

install_data('README.md',
  install_dir : join_paths(datadir, 'fwupd', 'remotes.d', 'vendor', 'firmware')
)

# replace @datadir@
con2 = configuration_data()
con2.set('datadir', datadir)
configure_file(
  input : 'vendor.conf',
  output : 'vendor.conf',
  configuration : con2,
  install: true,
  install_dir: join_paths(sysconfdir, 'fwupd', 'remotes.d'),
)
configure_file(
  input : 'vendor-directory.conf',
  output : 'vendor-directory.conf',
  configuration : con2,
  install: true,
  install_dir: join_paths(sysconfdir, 'fwupd', 'remotes.d'),
)

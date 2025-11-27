if build_standalone and get_option('lvfs') != 'false'
  con3 = configuration_data()
  con3.set('compression', lvfs_metadata_format)
  if get_option('lvfs') == 'disabled'
    con3.set('enabled', 'false')
  else
    con3.set('enabled', 'true')
  endif
  configure_file(
    input: 'lvfs.conf',
    output: 'lvfs.conf',
    configuration: con3,
    install: true,
    install_tag: 'runtime',
    install_dir: join_paths(sysconfdir, 'fwupd', 'remotes.d'),
  )
  configure_file(
    input: 'lvfs-testing.conf',
    output: 'lvfs-testing.conf',
    configuration: con3,
    install: true,
    install_tag: 'runtime',
    install_dir: join_paths(sysconfdir, 'fwupd', 'remotes.d'),
  )
  i18n.merge_file(
    input: 'lvfs.metainfo.xml',
    output: 'org.freedesktop.fwupd.remotes.lvfs.metainfo.xml',
    type: 'xml',
    po_dir: join_paths(meson.project_source_root(), 'po'),
    data_dirs: join_paths(meson.project_source_root(), 'po'),
    install: true,
    install_tag: 'runtime',
    install_dir: join_paths(get_option('datadir'), 'fwupd', 'metainfo'),
  )
  i18n.merge_file(
    input: 'lvfs-testing.metainfo.xml',
    output: 'org.freedesktop.fwupd.remotes.lvfs-testing.metainfo.xml',
    type: 'xml',
    po_dir: join_paths(meson.project_source_root(), 'po'),
    data_dirs: join_paths(meson.project_source_root(), 'po'),
    install: true,
    install_tag: 'runtime',
    install_dir: join_paths(get_option('datadir'), 'fwupd', 'metainfo'),
  )
endif

install_data(
  'README.md',
  install_tag: 'doc',
  install_dir: join_paths(datadir, 'fwupd', 'remotes.d', 'vendor', 'firmware'),
)

# replace @datadir@
con2 = configuration_data()
con2.set('datadir', datadir)
configure_file(
  input: 'vendor.conf',
  output: 'vendor.conf',
  configuration: con2,
  install: get_option('vendor_metadata'),
  install_tag: 'runtime',
  install_dir: join_paths(sysconfdir, 'fwupd', 'remotes.d'),
)
configure_file(
  input: 'vendor-directory.conf',
  output: 'vendor-directory.conf',
  configuration: con2,
  install: true,
  install_tag: 'runtime',
  install_dir: join_paths(sysconfdir, 'fwupd', 'remotes.d'),
)

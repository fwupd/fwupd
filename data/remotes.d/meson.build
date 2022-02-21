if build_standalone and get_option('lvfs') != 'false'
  install_data([
      'lvfs-testing.conf',
    ],
    install_dir : join_paths(sysconfdir, 'fwupd', 'remotes.d')
  )
  con3 = configuration_data()
  if get_option('lvfs') == 'disabled'
    con3.set('enabled', 'false')
  else
    con3.set('enabled', 'true')
  endif
  configure_file(
    input : 'lvfs.conf',
    output : 'lvfs.conf',
    configuration : con3,
    install: true,
    install_dir: join_paths(sysconfdir, 'fwupd', 'remotes.d'),
  )
  i18n.merge_file(
    input: 'lvfs.metainfo.xml',
    output: 'org.freedesktop.fwupd.remotes.lvfs.metainfo.xml',
    type: 'xml',
    po_dir: join_paths(meson.project_source_root(), 'po'),
    data_dirs: join_paths(meson.project_source_root(), 'po'),
    install: true,
    install_dir: join_paths(get_option('datadir'), 'fwupd', 'metainfo')
  )
  i18n.merge_file(
    input: 'lvfs-testing.metainfo.xml',
    output: 'org.freedesktop.fwupd.remotes.lvfs-testing.metainfo.xml',
    type: 'xml',
    po_dir: join_paths(meson.project_source_root(), 'po'),
    data_dirs: join_paths(meson.project_source_root(), 'po'),
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

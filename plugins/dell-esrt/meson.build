cargs = ['-DG_LOG_DOMAIN="FuPluginDellEsrt"']

install_data(['metadata.xml'],
  install_dir : join_paths(datadir, 'fwupd', 'remotes.d', 'dell-esrt')
)

shared_module('fu_plugin_dell_esrt',
  fu_hash,
  sources : [
    'fu-plugin-dell-esrt.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : [
    cargs,
  ],
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  dependencies : [
    plugin_deps,
    libsmbios_c,
  ],
)

# replace @datadir@
con2 = configuration_data()
con2.set('datadir', datadir)
configure_file(
  input : 'dell-esrt.conf',
  output : 'dell-esrt.conf',
  configuration : con2,
  install: true,
  install_dir: join_paths(sysconfdir, 'fwupd', 'remotes.d'),
)

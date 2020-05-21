cargs = ['-DG_LOG_DOMAIN="FuPluginMsr"']

install_data(['msr.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

install_data(['fwupd-msr.conf'],
  install_dir: join_paths(sysconfdir, 'modules-load.d')
)

shared_module('fu_plugin_msr',
  fu_hash,
  sources : [
    'fu-plugin-msr.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)

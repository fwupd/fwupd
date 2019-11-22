cargs = ['-DG_LOG_DOMAIN="FuPluginCsr"']

install_data(['csr-aiaiai.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_csr',
  fu_hash,
  sources : [
    'fu-csr-device.c',
    'fu-plugin-csr.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
    plugindfu_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
  link_with : [
    fwupdplugin,
    dfu,
  ],
)

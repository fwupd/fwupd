if get_option('plugin_cfu')
if not get_option('gudev')
  error('gudev is required for plugin_cfu')
endif
cargs = ['-DG_LOG_DOMAIN="FuPluginCfu"']

install_data([
  'cfu.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_cfu',
  fu_hash,
  sources : [
    'fu-cfu-device.c',
    'fu-cfu-module.c',
    'fu-plugin-cfu.c',
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
endif

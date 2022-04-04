if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginCorsair"']

install_data(['corsair.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_corsair',
  fu_hash,
  sources : [
    'fu-plugin-corsair.c',
    'fu-corsair-common.c',
    'fu-corsair-device.c',
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

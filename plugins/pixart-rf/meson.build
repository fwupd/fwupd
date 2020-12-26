cargs = ['-DG_LOG_DOMAIN="FuPluginPixartRf"']

shared_module('fu_plugin_pixart_rf',
  fu_hash,
  sources : [
    'fu-plugin-pixart-rf.c',
    'fu-pxi-device.c',
    'fu-pxi-firmware.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
  link_with : [
    fwupd,
    fwupdplugin,
  ],
)

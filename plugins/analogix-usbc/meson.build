cargs = ['-DG_LOG_DOMAIN="FuPluginANXUsbc"']

install_data(['analogix-usbc.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_analogix_usbc',
  fu_hash,
  sources : [
    'fu-plugin-analogix-usbc.c',
    'fu-analogix-usbc-device.c',
    'fu-analogix-usbc-common.c',
    'fu-analogix-usbc-firmware.c',
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

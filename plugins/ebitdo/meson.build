cargs = ['-DG_LOG_DOMAIN="FuPluginEbitdo"']

install_data(['ebitdo.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_ebitdo',
  fu_hash,
  sources : [
    'fu-plugin-ebitdo.c',
    'fu-ebitdo-common.c',
    'fu-ebitdo-device.c',
    'fu-ebitdo-firmware.c',
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

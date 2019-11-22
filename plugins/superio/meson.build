cargs = ['-DG_LOG_DOMAIN="FuPluginSuperio"']

install_data(['superio.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_superio',
  fu_hash,
  sources : [
    'fu-plugin-superio.c',
    'fu-superio-device.c',
    'fu-superio-it85-device.c',
    'fu-superio-it89-device.c',
    'fu-superio-common.c',
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

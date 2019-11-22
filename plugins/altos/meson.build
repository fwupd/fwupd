cargs = ['-DG_LOG_DOMAIN="FuPluginAltos"']

install_data(['altos.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_altos',
  fu_hash,
  sources : [
    'fu-altos-device.c',
    'fu-altos-firmware.c',
    'fu-plugin-altos.c',
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
    libelf,
    plugin_deps,
  ],
)

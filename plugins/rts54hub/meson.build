cargs = ['-DG_LOG_DOMAIN="FuPluginRts54Hub"']

install_data([
  'rts54hub.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_rts54hub',
  fu_hash,
  sources : [
    'fu-rts54hub-device.c',
    'fu-plugin-rts54hub.c',
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

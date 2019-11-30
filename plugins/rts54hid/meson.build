cargs = ['-DG_LOG_DOMAIN="FuPluginRts54Hid"']

install_data([
  'rts54hid.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_rts54hid',
  fu_hash,
  sources : [
    'fu-rts54hid-device.c',
    'fu-rts54hid-module.c',
    'fu-plugin-rts54hid.c',
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

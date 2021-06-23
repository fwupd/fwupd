if get_option('gudev')
cargs = ['-DG_LOG_DOMAIN="FuPluginEc"']

install_data(['ec.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_ec',
  fu_hash,
  sources : [
    'fu-plugin-ec.c',
    'fu-ec-device.c',
    'fu-ec-it5570-device.c',
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

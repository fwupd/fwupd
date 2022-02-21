if get_option('plugin_realtek_mst').require(gudev.found(),
    error_message: 'gudev is needed for plugin_realtek_mst').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginRealtekMst"']

install_data(['realtek-mst.quirk'],
  install_dir: join_paths(get_option('datadir'), 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_realtek_mst',
  fu_hash,
  sources : [
    'fu-realtek-mst-device.c',
    'fu-plugin-realtek-mst.c',
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
  c_args : [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  dependencies : [
    plugin_deps,
  ],
)
endif

cargs = ['-DG_LOG_DOMAIN="FuPluginGoodixMoc"']

install_data([
  'goodixmoc.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_goodixmoc',
  fu_hash,
  sources : [
    'fu-goodixmoc-common.c',
    'fu-goodixmoc-device.c',
    'fu-plugin-goodixmoc.c',
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

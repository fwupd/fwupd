cargs = ['-DG_LOG_DOMAIN="FuPluginGoodixFp"']

install_data([
  'goodixfp.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_goodixfp',
  fu_hash,
  sources : [
    'fu-goodixfp-common.c',
    'fu-goodixfp-device.c',
    'fu-plugin-goodixfp.c',
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

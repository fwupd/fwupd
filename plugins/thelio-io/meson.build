cargs = ['-DG_LOG_DOMAIN="FuPluginThelioIo"']

install_data(['thelio-io.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_thelio_io',
  fu_hash,
  sources : [
    'fu-plugin-thelio-io.c',
    'fu-thelio-io-device.c',
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

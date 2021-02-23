if get_option('plugin_emmc')
if not get_option('gudev')
  error('gudev is required for plugin_emmc')
endif
cargs = ['-DG_LOG_DOMAIN="FuPluginEmmc"']

install_data(['emmc.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)
shared_module('fu_plugin_emmc',
  fu_hash,
  sources : [
    'fu-plugin-emmc.c',
    'fu-emmc-device.c',
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

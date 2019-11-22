cargs = ['-DG_LOG_DOMAIN="FuPluginFastboot"']

install_data(['fastboot.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_fastboot',
  fu_hash,
  sources : [
    'fu-plugin-fastboot.c',
    'fu-fastboot-device.c',
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

cargs = ['-DG_LOG_DOMAIN="FuPluginDellDock"']

install_data(['dell-dock.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_dell_dock',
  fu_hash,
  sources : [
    'fu-plugin-dell-dock.c',
    'fu-dell-dock-common.c',
    'fu-dell-dock-hid.c',
    'fu-dell-dock-status.c',
    'fu-dell-dock-i2c-ec.c',
    'fu-dell-dock-hub.c',
    'fu-dell-dock-i2c-tbt.c',
    'fu-dell-dock-i2c-mst.c'
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
    gudev,
  ],
)

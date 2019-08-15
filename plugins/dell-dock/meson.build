cargs = ['-DG_LOG_DOMAIN="FuPluginDellDock"']

install_data(['dell-dock.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

install_data(['dell-dock.png'],
  install_dir : join_paths(datadir, 'icons', 'fwupd', 'plugins')
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
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    libfwupdprivate,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
    gudev,
  ],
)

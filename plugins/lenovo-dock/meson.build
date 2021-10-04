cargs = ['-DG_LOG_DOMAIN="FuPluginLenovoDock"']

install_data([
  'lenovo-dock.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_lenovo_dock',
  fu_hash,
  sources : [
    'fu-lenovo-dock-common.c',
    'fu-lenovo-dock-dmc-device.c',
    'fu-lenovo-dock-mcu-device.c',
    'fu-lenovo-dock-firmware.c',
    'fu-plugin-lenovo-dock.c',
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

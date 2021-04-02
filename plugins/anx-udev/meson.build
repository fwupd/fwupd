cargs = ['-DG_LOG_DOMAIN="FuPluginANXUDev"']

install_data(['anx-udev.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_anx_udev',
  fu_hash,
  sources : [
    'fu-plugin-anx-udev.c',
    'fu-anx-udev-device.c',
    'fu-anx-udev-common.c',
    'fu-anx-udev-firmware.c',
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

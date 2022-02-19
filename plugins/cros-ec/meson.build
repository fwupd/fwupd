if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginCrosEc"']

install_data(['cros-ec.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_cros_ec',
  fu_hash,
  sources : [
    'fu-plugin-cros-ec.c',
    'fu-cros-ec-usb-device.c',
    'fu-cros-ec-common.c',      # fuzzing
    'fu-cros-ec-firmware.c',    # fuzzing
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

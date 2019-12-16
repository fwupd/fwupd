cargs = ['-DG_LOG_DOMAIN="FuPluginVliUsbhub"']

install_data([
  'vli-pd.quirk',
  'vli-usbhub-lenovo.quirk',
  'vli-usbhub.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_vli',
  fu_hash,
  sources : [
    'fu-plugin-vli.c',
    'fu-vli-common.c',
    'fu-vli-device.c',
    'fu-vli-pd-device.c',
    'fu-vli-usbhub-common.c',
    'fu-vli-usbhub-device.c',
    'fu-vli-usbhub-firmware.c',
    'fu-vli-usbhub-i2c-device.c',
    'fu-vli-usbhub-pd-common.c',
    'fu-vli-usbhub-pd-device.c',
    'fu-vli-usbhub-pd-firmware.c',
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

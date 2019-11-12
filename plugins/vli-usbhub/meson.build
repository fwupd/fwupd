cargs = ['-DG_LOG_DOMAIN="FuPluginVliUsbhub"']

install_data([
  'vli-usbhub.quirk',
  'vli-usbhub-lenovo.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_vli_usbhub',
  fu_hash,
  sources : [
    'fu-plugin-vli-usbhub.c',
    'fu-vli-usbhub-common.c',
    'fu-vli-usbhub-device.c',
    'fu-vli-usbhub-firmware.c',
    'fu-vli-usbhub-pd-common.c',
    'fu-vli-usbhub-pd-firmware.c',
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
  ],
)

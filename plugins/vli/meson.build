if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginVliUsbhub"']

plugin_quirks += files([
  'vli-bizlink.quirk',
  'vli-dell.quirk',
  'vli-goodway.quirk',
  'vli-hyper.quirk',
  'vli-lenovo.quirk',
  'vli-samsung.quirk',
  ])
plugin_builtins += static_library('fu_plugin_vli',
  structgen.process('fu-vli.struct'),
  sources: [
    'fu-vli-plugin.c',
    'fu-vli-common.c',
    'fu-vli-device.c',
    'fu-vli-pd-common.c',
    'fu-vli-pd-device.c',
    'fu-vli-pd-firmware.c',
    'fu-vli-pd-parade-device.c',
    'fu-vli-usbhub-device.c',
    'fu-vli-usbhub-firmware.c',
    'fu-vli-usbhub-i2c-common.c',
    'fu-vli-usbhub-msp430-device.c',
    'fu-vli-usbhub-pd-device.c',
    'fu-vli-usbhub-rtd21xx-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif

if libusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginParadeUsbhub"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('parade-usbhub.quirk')
plugin_builtins += static_library('fu_plugin_parade_usbhub',
  rustgen.process('fu-parade-usbhub.rs'),
  sources: [
    'fu-parade-usbhub-device.c',
    'fu-parade-usbhub-firmware.c',
    'fu-parade-usbhub-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files(
  'tests/parade-ps5512evb.json',
)
endif

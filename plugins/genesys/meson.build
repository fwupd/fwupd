cargs = ['-DG_LOG_DOMAIN="FuPluginGenesys"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('genesys.quirk', 'genesys-usbhub-fwbank.quirk')
plugin_builtins += static_library('fu_plugin_genesys',
  rustgen.process(
    'fu-genesys-usbhub.rs', # fuzzing
  ),
  sources: [
    'fu-genesys-scaler-firmware.c',   # fuzzing
    'fu-genesys-usbhub-firmware.c',   # fuzzing
    'fu-genesys-usbhub-dev-firmware.c', # fuzzing
    'fu-genesys-usbhub-pd-firmware.c', # fuzzing
    'fu-genesys-usbhub-codesign-firmware.c', # fuzzing
    'fu-genesys-scaler-device.c',
    'fu-genesys-usbhub-device.c',
    'fu-genesys-hubhid-device.c',
    'fu-genesys-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files(
  'tests/genesys-evb.json',
#  'tests/genesys-evb-failure.json', # cannot emulate two devices(usb hub + child hid).
  'tests/genesys-servo-dock.json',
  'tests/genesys-zdn-chromebook.json',
)

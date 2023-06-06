if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginGenesys"']

plugin_quirks += files('genesys.quirk')
plugin_builtins += static_library('fu_plugin_genesys',
  rustgen.process('fu-genesys-usbhub.rs'),
  sources: [
    'fu-genesys-scaler-firmware.c',   # fuzzing
    'fu-genesys-usbhub-firmware.c',   # fuzzing
    'fu-genesys-scaler-device.c',
    'fu-genesys-usbhub-device.c',
    'fu-genesys-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif

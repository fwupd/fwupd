if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginCrosEc"']

plugin_quirks += files('cros-ec.quirk')
plugin_builtins += static_library('fu_plugin_cros_ec',
  sources: [
    'fu-cros-ec-plugin.c',
    'fu-cros-ec-usb-device.c',
    'fu-cros-ec-common.c',      # fuzzing
    'fu-cros-ec-firmware.c',    # fuzzing
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif

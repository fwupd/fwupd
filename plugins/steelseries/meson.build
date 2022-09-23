if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginSteelSeries"']

plugin_quirks += files('steelseries.quirk')
plugin_builtins += static_library('fu_plugin_steelseries',
  sources: [
    'fu-steelseries-plugin.c',
    'fu-steelseries-device.c',
    'fu-steelseries-firmware.c',
    'fu-steelseries-fizz-hid.c',
    'fu-steelseries-fizz-tunnel.c',
    'fu-steelseries-fizz.c',
    'fu-steelseries-mouse.c',
    'fu-steelseries-gamepad.c',
    'fu-steelseries-sonic.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif

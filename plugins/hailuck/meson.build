if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginNovatek"']

plugin_quirks += files('hailuck.quirk')
plugin_builtins += static_library('fu_plugin_hailuck',
  sources: [
    'fu-hailuck-common.c',
    'fu-hailuck-bl-device.c',
    'fu-hailuck-kbd-device.c',
    'fu-hailuck-kbd-firmware.c',  # fuzzing
    'fu-hailuck-tp-device.c',
    'fu-hailuck-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif

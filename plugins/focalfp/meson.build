if gudev.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginFocalfp"']

plugin_quirks += files('focalfp.quirk')
plugin_builtins += static_library('fu_plugin_focalfp',
  sources: [
    'fu-focalfp-plugin.c',
    'fu-focalfp-firmware.c',
    'fu-focalfp-hid-device.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)
endif

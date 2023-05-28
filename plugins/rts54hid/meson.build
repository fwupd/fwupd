if gusb.found()
  cargs = ['-DG_LOG_DOMAIN="FuPluginRts54Hid"']

  plugin_quirks += files('rts54hid.quirk')
  plugin_builtins += static_library(
    'fu_plugin_rts54hid',
    sources: [
      'fu-rts54hid-device.c',
      'fu-rts54hid-module.c',
      'fu-rts54hid-plugin.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
  )
endif

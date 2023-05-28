if gusb.found()
  cargs = ['-DG_LOG_DOMAIN="FuPluginRts54Hub"']

  plugin_quirks += files('rts54hub.quirk')
  plugin_builtins += static_library(
    'fu_plugin_rts54hub',
    sources: [
      'fu-rts54hub-device.c',
      'fu-rts54hub-rtd21xx-device.c',
      'fu-rts54hub-rtd21xx-background.c',
      'fu-rts54hub-rtd21xx-foreground.c',
      'fu-rts54hub-plugin.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
  )
endif

if gusb.found()
  cargs = ['-DG_LOG_DOMAIN="FuPluginColorHug"']

  plugin_quirks += files('colorhug.quirk')
  plugin_builtins += static_library(
    'fu_plugin_colorhug',
    sources: [
      'fu-colorhug-common.c',
      'fu-colorhug-device.c',
      'fu-colorhug-plugin.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
  )
endif

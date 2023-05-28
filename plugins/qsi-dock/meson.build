if gusb.found()
  cargs = ['-DG_LOG_DOMAIN="FuPluginQsiDock"']

  plugin_quirks += files('qsi-dock.quirk')
  plugin_builtins += static_library(
    'fu_plugin_qsi_dock',
    sources: [
      'fu-qsi-dock-child-device.c',
      'fu-qsi-dock-mcu-device.c',
      'fu-qsi-dock-plugin.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
  )
endif

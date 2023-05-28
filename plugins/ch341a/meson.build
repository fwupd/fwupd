if gusb.found()
  cargs = ['-DG_LOG_DOMAIN="FuPluginCh341a"']

  plugin_quirks += files('ch341a.quirk')
  plugin_builtins += static_library(
    'fu_plugin_ch341a',
    sources: [
      'fu-ch341a-cfi-device.c',
      'fu-ch341a-device.c',
      'fu-ch341a-plugin.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
  )
endif

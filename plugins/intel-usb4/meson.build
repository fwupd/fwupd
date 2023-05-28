if gusb.found()
  cargs = ['-DG_LOG_DOMAIN="FuPluginIntelUsb4"']

  plugin_quirks += files('intel-usb4.quirk')
  plugin_builtins += static_library(
    'fu_plugin_intel_usb4',
    rustgen.process('fu-intel-usb4.rs'),
    sources: [
      'fu-intel-usb4-device.c',
      'fu-intel-usb4-plugin.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: [
      plugin_deps,
      gudev,
    ],
  )
endif

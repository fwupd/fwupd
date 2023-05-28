if gusb.found()
  cargs = ['-DG_LOG_DOMAIN="FuPluginGoodixtp"']

  plugin_quirks += files('goodixtp.quirk')
  plugin_builtins += static_library(
    'fu_plugin_goodixtp',
    rustgen.process('fu-goodixtp.rs'),
    sources: [
      'fu-goodixtp-brlb-device.c',
      'fu-goodixtp-brlb-firmware.c',
      'fu-goodixtp-firmware.c',
      'fu-goodixtp-gtx8-device.c',
      'fu-goodixtp-gtx8-firmware.c',
      'fu-goodixtp-hid-device.c',
      'fu-goodixtp-plugin.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
  )
endif

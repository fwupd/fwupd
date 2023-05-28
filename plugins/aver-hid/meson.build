if gusb.found()
  cargs = ['-DG_LOG_DOMAIN="FuPluginAverHid"']

  plugin_quirks += files('aver-hid.quirk')
  plugin_builtins += static_library(
    'fu_plugin_aver_hid',
    rustgen.process('fu-aver-hid.rs'),
    sources: [
      'fu-aver-hid-device.c',
      'fu-aver-hid-firmware.c',
      'fu-aver-hid-plugin.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
  )
endif

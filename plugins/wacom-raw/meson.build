if gudev.found()
  cargs = ['-DG_LOG_DOMAIN="FuPluginWacomRaw"']

  plugin_quirks += files('wacom-raw.quirk')
  plugin_builtins += static_library(
    'fu_plugin_wacom_raw',
    sources: [
      'fu-wacom-raw-plugin.c',
      'fu-wacom-common.c',
      'fu-wacom-device.c',
      'fu-wacom-aes-device.c',
      'fu-wacom-emr-device.c',
    ],
    include_directories: plugin_incdirs,
    c_args: cargs,
    dependencies: plugin_deps,
    link_with: plugin_libs,
  )
endif

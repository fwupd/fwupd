if get_option(
  'plugin_fastboot'
).require(gusb.found(), error_message: 'gusb is needed for plugin_fastboot').allowed()
  cargs = ['-DG_LOG_DOMAIN="FuPluginFastboot"']

  plugin_quirks += files('fastboot.quirk')
  plugin_builtins += static_library(
    'fu_plugin_fastboot',
    sources: [
      'fu-fastboot-plugin.c',
      'fu-fastboot-device.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
  )
endif

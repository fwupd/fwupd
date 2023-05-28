if get_option(
  'plugin_parade_lspcon'
).require(
  gudev.found(),
  error_message: 'gudev is needed for plugin_parade_lspcon',
).allowed()

  cargs = ['-DG_LOG_DOMAIN="FuPluginParadeLspcon"']

  plugin_quirks += files('parade-lspcon.quirk')
  plugin_builtins += static_library(
    'fu_plugin_parade_lspcon',
    sources: [
      'fu-parade-lspcon-device.c',
      'fu-parade-lspcon-plugin.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: [
      cargs,
      '-DLOCALSTATEDIR="' + localstatedir + '"',
    ],
    dependencies: plugin_deps,
  )
endif

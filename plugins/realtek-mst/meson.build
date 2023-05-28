if get_option(
  'plugin_realtek_mst'
).require(
  gudev.found(),
  error_message: 'gudev is needed for plugin_realtek_mst',
).allowed()
  cargs = ['-DG_LOG_DOMAIN="FuPluginRealtekMst"']

  plugin_quirks += files('realtek-mst.quirk')
  plugin_builtins += static_library(
    'fu_plugin_realtek_mst',
    sources: [
      'fu-realtek-mst-device.c',
      'fu-realtek-mst-plugin.c',
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

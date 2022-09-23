if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginGoodixMoc"']

plugin_quirks += files('goodixmoc.quirk')
plugin_builtins += static_library('fu_plugin_goodixmoc',
  sources: [
    'fu-goodixmoc-device.c',
    'fu-goodixmoc-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif

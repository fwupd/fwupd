if gudev.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginAspeedAst2x00"']

plugin_quirks += files('aspeed-ast2x00.quirk')
plugin_builtins += static_library('fu_plugin_aspeed_ast2x00',
  sources: [
    'fu-aspeed-ast2x00-device.c',
    'fu-aspeed-ast2x00-native-device.c',
    'fu-aspeed-ast2x00-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif

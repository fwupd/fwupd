if gudev.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginOptionrom"']

plugin_quirks += files('optionrom.quirk')
plugin_builtins += static_library('fu_plugin_optionrom',
  sources: [
    'fu-optionrom-plugin.c',
    'fu-optionrom-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif

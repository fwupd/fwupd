if get_option('plugin_cfu').require(gudev.found(),
    error_message: 'gudev is needed for plugin_cfu').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginCfu"']

plugin_quirks += files('cfu.quirk')
plugin_builtins += static_library('fu_plugin_cfu',
  structgen.process(
    'fu-cfu.struct',
  ),
  sources: [
    'fu-cfu-device.c',
    'fu-cfu-module.c',
    'fu-cfu-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif

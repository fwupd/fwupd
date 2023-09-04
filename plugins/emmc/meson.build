if get_option('plugin_emmc').require(gudev.found(),
    error_message: 'gudev is needed for plugin_emmc').allowed()

cargs = ['-DG_LOG_DOMAIN="FuPluginEmmc"']

plugin_quirks += files('emmc.quirk')
plugin_builtins += static_library('fu_plugin_emmc',
  sources: [
    'fu-emmc-plugin.c',
    'fu-emmc-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif

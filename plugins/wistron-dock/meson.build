if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginWistronDock"']

plugin_quirks += files('wistron-dock.quirk')
plugin_builtins += static_library('fu_plugin_wistron_dock',
  sources: [
    'fu-wistron-dock-common.c',
    'fu-wistron-dock-device.c',
    'fu-wistron-dock-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif

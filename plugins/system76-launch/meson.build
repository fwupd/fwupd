if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginSystem76Launch"']

plugin_quirks += files('system76-launch.quirk')
plugin_builtins += static_library('fu_plugin_system76_launch',
  sources: [
    'fu-system76-launch-plugin.c',
    'fu-system76-launch-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif

if gusb.found()

cargs = ['-DG_LOG_DOMAIN="FuPluginLogitechScribe"']

plugin_quirks += files('logitech-scribe.quirk')

plugin_builtins += static_library('fu_plugin_logitech_scribe',
  sources: [
    'fu-logitech-scribe-device.c',
    'fu-logitech-scribe-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
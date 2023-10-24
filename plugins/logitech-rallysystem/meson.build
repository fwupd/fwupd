if gudev.found() and gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginLogitechRallysystem"']

plugin_quirks += files('logitech-rallysystem.quirk')

plugin_builtins += static_library('fu_plugin_logitech_rallysystem',
  rustgen.process('fu-logitech-rallysystem.rs'),
  sources: [
    'fu-logitech-rallysystem-plugin.c',
    'fu-logitech-rallysystem-audio-device.c',
    'fu-logitech-rallysystem-tablehub-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif

if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginAnalogix"']

plugin_quirks += files('analogix.quirk')
plugin_builtins += static_library('fu_plugin_analogix',
  structgen.process('fu-analogix.struct'),
  sources: [
    'fu-analogix-plugin.c',
    'fu-analogix-device.c',
    'fu-analogix-firmware.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif

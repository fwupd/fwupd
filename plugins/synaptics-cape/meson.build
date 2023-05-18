if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginSynapticsCape"']

plugin_quirks += files('synaptics-cape.quirk')
plugin_builtins += static_library('fu_plugin_synaptics_cape',
  sources: [
    'fu-synaptics-cape-plugin.c',
    'fu-synaptics-cape-device.c',
    'fu-synaptics-cape-firmware.c', # fuzzing
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif

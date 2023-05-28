if gusb.found()
  cargs = ['-DG_LOG_DOMAIN="FuPluginSynapticsCxaudio"']

  plugin_quirks += files('synaptics-cxaudio.quirk')
  plugin_builtins += static_library(
    'fu_plugin_synaptics_cxaudio',
    rustgen.process('fu-synaptics-cxaudio.rs'),
    sources: [
      'fu-synaptics-cxaudio-plugin.c',
      'fu-synaptics-cxaudio-device.c',
      'fu-synaptics-cxaudio-firmware.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
  )
endif

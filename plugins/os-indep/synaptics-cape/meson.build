cargs = ['-DG_LOG_DOMAIN="FuPluginSynapticsCape"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('synaptics-cape.quirk')
plugin_builtins += static_library('fu_plugin_synaptics_cape',
  rustgen.process(
    'fu-synaptics-cape.rs',     # fuzzing
  ),
  sources: [
    'fu-synaptics-cape-plugin.c',
    'fu-synaptics-cape-device.c',
    'fu-synaptics-cape-firmware.c',     # fuzzing
    'fu-synaptics-cape-hid-firmware.c', # fuzzing
    'fu-synaptics-cape-sngl-firmware.c', # fuzzing
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

cargs = ['-DG_LOG_DOMAIN="FuPluginSynapticsVmm9"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('synaptics-vmm9.quirk')
plugin_builtins += static_library('fu_plugin_synaptics_vmm9',
  rustgen.process('fu-synaptics-vmm9.rs'),
  sources: [
    'fu-synaptics-vmm9-device.c',
    'fu-synaptics-vmm9-firmware.c',
    'fu-synaptics-vmm9-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files('tests/synaptics-vmm9430evb.json')

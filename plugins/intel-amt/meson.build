if host_machine.system() == 'linux'
cargs = ['-DG_LOG_DOMAIN="FuPluginIntelAmt"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('intel-amt.quirk')
plugin_builtins += static_library('fu_plugin_intel_amt',
  rustgen.process(
    'fu-intel-amt.rs',
  ),
  sources: [
    'fu-intel-amt-plugin.c',
    'fu-intel-amt-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

enumeration_data += files('tests/intel-amt-setup.json')
device_tests += files('tests/intel-amt.json')
endif

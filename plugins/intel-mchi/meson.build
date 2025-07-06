if host_machine.system() != 'linux'
  subdir_done()
endif
cargs = ['-DG_LOG_DOMAIN="FuPluginIntelMchi"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('intel-mchi.quirk')
plugin_builtins += static_library('fu_plugin_intel_mchi',
  sources: [
    'fu-intel-mchi-plugin.c',
    'fu-intel-mchi-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
enumeration_data += files('tests/intel-mchi-setup.json')
device_tests += files('tests/intel-mchi.json')

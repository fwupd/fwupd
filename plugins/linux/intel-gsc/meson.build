cargs = ['-DG_LOG_DOMAIN="FuPluginIgsc"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('igsc.quirk')
plugin_builtins += static_library('fu_plugin_igsc',
  rustgen.process('fu-igsc.rs'),
  sources: [
    'fu-igsc-plugin.c',
    'fu-igsc-device.c',
    'fu-igsc-code-firmware.c',
    'fu-igsc-oprom-firmware.c',
    'fu-igsc-aux-device.c',
    'fu-igsc-aux-firmware.c',
    'fu-igsc-oprom-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
enumeration_data += files('tests/intel-gsc-setup.json')
device_tests += files('tests/intel-gsc.json')

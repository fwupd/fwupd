cargs = ['-DG_LOG_DOMAIN="FuPluginAnalogix"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('analogix.quirk')
plugin_builtins += static_library('fu_plugin_analogix',
  rustgen.process('fu-analogix.rs'),
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

device_tests += files(
  'tests/analogix-anx7518.json',
)

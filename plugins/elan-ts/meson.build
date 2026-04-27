host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginElanTs"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('elan-ts.quirk')
plugin_builtins += static_library('fu_plugin_elan_ts',
  rustgen.process('fu-elan-ts.rs'),
  sources: [
    'fu-elan-ts-plugin.c',
    'fu-elan-ts-firmware.c',
    'fu-elan-ts-device.c',
  ],
  include_directories: plugin_incdirs,
  c_args: cargs,
  link_with: plugin_libs,
  dependencies: plugin_deps,
)

device_tests += files('tests/elan-ts.json')

cargs = ['-DG_LOG_DOMAIN="FuPluginHpiCfu"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('hpi-cfu.quirk')
plugin_builtins += static_library('fu_plugin_hpi_cfu',
  rustgen.process('fu-hpi-cfu.rs'),
  sources: [
    'fu-hpi-cfu-device.c',
    'fu-hpi-cfu-plugin.c',
  ],
  include_directories: [ 
    plugin_incdirs,
    plugincfu_incdir,
  ],
  link_with: [ 
    plugin_libs,
    plugin_builtin_cfu,
  ],
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files(
  'tests/hp-dock-g6-ultra.json',
)

cargs = ['-DG_LOG_DOMAIN="FuPluginCfu"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('cfu.quirk')
plugin_builtins += static_library('fu_plugin_cfu',
  sources: [
    'fu-cfu-device.c',
    'fu-cfu-module.c',
    'fu-cfu-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugincfu_incdir = include_directories('.')

enumeration_data += files('tests/microsoft-usbc-travel-hub-setup.json')
device_tests += files('tests/microsoft-usbc-travel-hub.json')

cargs = ['-DG_LOG_DOMAIN="FuPluginLegionHid"']

plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('legion-hid.quirk')

plugin_builtins += static_library('fu_plugin_legion_hid',
  rustgen.process('fu-legion-hid.rs'),
  sources: [
    'fu-legion-hid-plugin.c',
    'fu-legion-hid-child.c',
    'fu-legion-hid-device.c',
    'fu-legion-hid-firmware.c'
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files('tests/legion-hid.json')

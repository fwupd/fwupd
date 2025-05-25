cargs = ['-DG_LOG_DOMAIN="FuPluginLegionHid2"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('legion-hid2.quirk')
plugin_builtins += static_library('fu_plugin_legion_hid2',
  rustgen.process('fu-legion-hid2.rs'),
  sources: [
    'fu-legion-hid2-device.c',
    'fu-legion-hid2-sipo-device.c',
    'fu-legion-hid2-bl-device.c',
    'fu-legion-hid2-plugin.c',
    'fu-legion-hid2-firmware.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
device_tests += files('tests/legion-hid2.json')

cargs = ['-DG_LOG_DOMAIN="FuPluginTelinkDfu"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('telink-dfu.quirk')
plugin_builtins += static_library('fu_plugin_telink_dfu',
  rustgen.process('fu-telink-dfu.rs'),
  sources: [
    'fu-telink-dfu-archive.c',
    'fu-telink-dfu-common.c',
    'fu-telink-dfu-ble-device.c',
    'fu-telink-dfu-hid-device.c',
    'fu-telink-dfu-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files(
  'tests/telink-ryder-dongle.json',
)


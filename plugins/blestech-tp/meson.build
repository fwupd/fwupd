cargs = ['-DG_LOG_DOMAIN="FuPluginBlestechTp"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('blestech-tp.quirk')
plugin_builtins += static_library('fu_plugin_blestech_tp',
  rustgen.process('fu-blestech-tp.rs'),
  sources: [
    'fu-blestech-tp-firmware.c',
    'fu-blestech-tp-hid-device.c',
    'fu-blestech-tp-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files(
  'tests/blestech-touchpad.json'
)

cargs = ['-DG_LOG_DOMAIN="FuPluginLenovoAccessory"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('lenovo-accessory.quirk')
plugin_builtins += static_library('fu_plugin_lenovo_accessory',
  rustgen.process('fu-lenovo-accessory.rs'),
  sources: [
    'fu-lenovo-accessory-ble-device.c',
    'fu-lenovo-accessory-hid-bootloader.c',
    'fu-lenovo-accessory-hid-common.c',
    'fu-lenovo-accessory-hid-device.c',
    'fu-lenovo-accessory-impl.c',
    'fu-lenovo-accessory-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files(
  'tests/lenovo-accessory-700-keyboard.json',
  'tests/lenovo-accessory-700-mouse.json',
  'tests/lenovo-accessory-receiver.json',
)

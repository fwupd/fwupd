cargs = ['-DG_LOG_DOMAIN="FuPluginLenovoAccessory"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

plugin_quirks += files('lenovo-accessory.quirk')
plugin_rustgen_output = rustgen.process('fu-lenovo-accessory.rs')
plugin_builtin_lenovo_accessory = static_library(
  'fu_plugin_lenovo_accessory',
  plugin_rustgen_output,
  sources: [
    'fu-lenovo-accessory-ble-device.c',
    'fu-lenovo-accessory-firmware.c',
    'fu-lenovo-accessory-hid-bootloader.c',
    'fu-lenovo-accessory-hid-child-device.c',
    'fu-lenovo-accessory-hid-common.c',
    'fu-lenovo-accessory-hid-device.c',
    'fu-lenovo-accessory-hid-dual-device.c',
    'fu-lenovo-accessory-impl.c',
    'fu-lenovo-accessory-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_lenovo_accessory

device_tests += files(
  'tests/lenovo-accessory-6202-receiver.json',
  'tests/lenovo-accessory-700-keyboard.json',
  'tests/lenovo-accessory-700-mouse.json',
  'tests/lenovo-accessory-700-mouse-receiver.json',
  'tests/lenovo-accessory-ai-mouse.json',
  'tests/lenovo-accessory-receiver.json',
)

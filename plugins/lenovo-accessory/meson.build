cargs = ['-DG_LOG_DOMAIN="FuPluginLenovoAccessory"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('lenovo-accessory.quirk')
plugin_builtins += static_library('fu_plugin_lenovo_accessory',
  rustgen.process('fu-lenovo-accessory.rs'),
  sources: [
    'fu-lenovo-accessory-ble-command.c',
    'fu-lenovo-accessory-ble-device.c',
    'fu-lenovo-accessory-hid-bootloader.c',
    'fu-lenovo-accessory-hid-command.c',
    'fu-lenovo-accessory-hid-device.c',
    'fu-lenovo-accessory-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

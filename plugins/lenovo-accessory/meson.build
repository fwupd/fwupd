cargs = ['-DG_LOG_DOMAIN="FuPluginLenovoHid"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('lenovo-accessory.quirk')
plugin_builtins += static_library('fu_plugin_lenovo_accessory',
  rustgen.process('fu-lenovo-accessory.rs'),
  sources: [
    'fu-lenovo-hid-bootloader.c',
    'fu-lenovo-accessory-hid-device.c',
    'fu-lenovo-accessory-plugin.c',
    'fu-lenovo-hid-firmware.c',
    'fu-lenovo-accessory-command.c',
    'fu-lenovo-hid-firmware.c'
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

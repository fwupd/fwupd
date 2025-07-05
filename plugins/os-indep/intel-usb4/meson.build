cargs = ['-DG_LOG_DOMAIN="FuPluginIntelUsb4"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('intel-usb4.quirk')
plugin_builtins += static_library('fu_plugin_intel_usb4',
  rustgen.process('fu-intel-usb4.rs'),
  sources: [
    'fu-intel-usb4-device.c',
    'fu-intel-usb4-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

enumeration_data += files('tests/intel-gatkex-setup.json')
device_tests += files('tests/intel-gatkex.json')

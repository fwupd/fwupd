cargs = ['-DG_LOG_DOMAIN="FuPluginHuddlyUsb"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('huddly-usb.quirk')
plugin_builtins += static_library('fu_plugin_huddly_usb',
  rustgen.process('fu-huddly-usb.rs'),
  sources: [
    'fu-huddly-usb-common.c',
    'fu-huddly-usb-device.c',
    'fu-huddly-usb-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

enumeration_data += files('tests/huddly-s1-basic-setup.json')
device_tests += files(
  'tests/huddly-s1.json',
  'tests/huddly-s1-basic.json',
)

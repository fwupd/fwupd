cargs = ['-DG_LOG_DOMAIN="FuPluginElanKbd"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('elan-kbd.quirk')
plugin_builtins += static_library('fu_plugin_elan_kbd',
  rustgen.process('fu-elan-kbd.rs'),
  sources: [
    'fu-elan-kbd-debug-device.c',
    'fu-elan-kbd-device.c',
    'fu-elan-kbd-firmware.c',
    'fu-elan-kbd-plugin.c',
    'fu-elan-kbd-runtime.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files('tests/elan-kbd.json')

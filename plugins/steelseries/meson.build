cargs = ['-DG_LOG_DOMAIN="FuPluginSteelSeries"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('steelseries.quirk')
plugin_builtins += static_library('fu_plugin_steelseries',
  rustgen.process([
    'fu-steelseries-fizz.rs',
    'fu-steelseries-gamepad.rs',
    'fu-steelseries-sonic.rs',
  ]),
  sources: [
    'fu-steelseries-plugin.c',
    'fu-steelseries-device.c',
    'fu-steelseries-firmware.c',
    'fu-steelseries-fizz-hid.c',
    'fu-steelseries-fizz-tunnel.c',
    'fu-steelseries-fizz.c',
    'fu-steelseries-fizz-gen1.c',
    'fu-steelseries-fizz-gen2.c',
    'fu-steelseries-fizz-impl.c',
    'fu-steelseries-mouse.c',
    'fu-steelseries-gamepad.c',
    'fu-steelseries-sonic.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files(
  'tests/steelseries-aerox-3-wireless.json',
  'tests/steelseries-nova5.json',
  'tests/steelseries-rival-3-wireless.json',
  'tests/steelseries-stratus-duo.json',
  'tests/steelseries-stratus-duo-rx.json',
)
